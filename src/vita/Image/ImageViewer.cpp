#include "Image/ImageViewer.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/I18n.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "UI/Widgets.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Math.hpp"
#include "Utils/Text.hpp"
#include "nanovg.h"

#include <psp2/io/fcntl.h>

extern "C" {
#include <fcntl.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace {

void setImageError(const char* fileName, const char* message) {
    lockScan();
    gImage.loaded = 0;
    gImage.error = 1;
    copyText(gImage.fileName, sizeof(gImage.fileName), fileName);
    copyText(gImage.message, sizeof(gImage.message), message);
    unlockScan();
}

bool readLocalFileToMemory(const char* path, uint64_t expectedSize, unsigned char** outData, int* outSize) {
    if (!path || !outData || !outSize || expectedSize == 0 || expectedSize > kMaxImageBytes) return false;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    unsigned char* data = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(expectedSize)));
    if (!data) {
        sceIoClose(fd);
        return false;
    }

    uint64_t total = 0;
    while (total < expectedSize) {
        const uint64_t remaining = expectedSize - total;
        const SceSize chunk = remaining > 256 * 1024 ? 256 * 1024 : static_cast<SceSize>(remaining);
        const SceSSize rc = sceIoRead(fd, data + total, chunk);
        if (rc <= 0) {
            std::free(data);
            sceIoClose(fd);
            return false;
        }
        total += static_cast<uint64_t>(rc);
    }

    sceIoClose(fd);
    *outData = data;
    *outSize = static_cast<int>(total);
    return true;
}

bool readSmbFileToMemory(const char* path, uint64_t expectedSize, unsigned char** outData, int* outSize) {
    if (!path || !outData || !outSize || expectedSize == 0 || expectedSize > kMaxImageBytes) return false;

    struct smb2_context* smb = smb2_init_context();
    if (!smb) return false;
    smb2_set_timeout(smb, 8);
    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (optionalText(gConn.domain)) smb2_set_domain(smb, gConn.domain);
    if (optionalText(gConn.password)) smb2_set_password(smb, gConn.password);

    if (smb2_connect_share(smb, gConn.server, gConn.share, optionalText(gConn.user)) < 0) {
        smb2_destroy_context(smb);
        return false;
    }

    struct smb2fh* fh = smb2_open(smb, path, O_RDONLY);
    if (!fh) {
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    uint64_t size = expectedSize;
    struct smb2_stat_64 st = {};
    if (smb2_fstat(smb, fh, &st) == 0 && st.smb2_size > 0) {
        size = static_cast<uint64_t>(st.smb2_size);
    }
    if (size == 0 || size > kMaxImageBytes) {
        smb2_close(smb, fh);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    unsigned char* data = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(size)));
    if (!data) {
        smb2_close(smb, fh);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    uint64_t total = 0;
    while (total < size) {
        const uint64_t remaining = size - total;
        const uint32_t chunk = remaining > 256 * 1024 ? 256 * 1024 : static_cast<uint32_t>(remaining);
        const int rc = smb2_read(smb, fh, data + total, chunk);
        if (rc <= 0) {
            std::free(data);
            smb2_close(smb, fh);
            smb2_disconnect_share(smb);
            smb2_destroy_context(smb);
            return false;
        }
        total += static_cast<uint64_t>(rc);
    }

    smb2_close(smb, fh);
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);
    *outData = data;
    *outSize = static_cast<int>(total);
    return true;
}

} // namespace

float fitImageScale(int width, int height, int rotationDegrees) {
    if (width <= 0 || height <= 0) return 1.0f;
    const bool rotated = rotationDegrees == 90 || rotationDegrees == 270;
    const float viewW = rotated ? static_cast<float>(height) : static_cast<float>(width);
    const float viewH = rotated ? static_cast<float>(width) : static_cast<float>(height);
    const float scaleX = kWidth / viewW;
    const float scaleY = kHeight / viewH;
    const float scale = scaleX < scaleY ? scaleX : scaleY;
    return scale < 1.0f ? scale : 1.0f;
}

void clampImageViewLocked() {
    if (!gImage.loaded || gImage.width <= 0 || gImage.height <= 0) {
        gImage.offsetX = 0.0f;
        gImage.offsetY = 0.0f;
        return;
    }

    const bool rotated = gImage.rotationDegrees == 90 || gImage.rotationDegrees == 270;
    const float displayedW = static_cast<float>(rotated ? gImage.height : gImage.width) * gImage.zoom;
    const float displayedH = static_cast<float>(rotated ? gImage.width : gImage.height) * gImage.zoom;
    const float maxX = displayedW > kWidth ? (displayedW - kWidth) * 0.5f : 0.0f;
    const float maxY = displayedH > kHeight ? (displayedH - kHeight) * 0.5f : 0.0f;
    gImage.offsetX = clampFloat(gImage.offsetX, -maxX, maxX);
    gImage.offsetY = clampFloat(gImage.offsetY, -maxY, maxY);
}

void resetImageView() {
    lockScan();
    gImage.zoom = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    clampImageViewLocked();
    unlockScan();
}

void closeImage(NVGcontext* vg) {
    lockScan();
    const int image = gImage.nvgImage;
    gImage.nvgImage = 0;
    gImage.loaded = 0;
    gImage.error = 0;
    gImage.width = 0;
    gImage.height = 0;
    gImage.rotationDegrees = 0;
    gImage.hudVisible = 1;
    gImage.zoom = 1.0f;
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    gImage.fileName[0] = '\0';
    gImage.message[0] = '\0';
    unlockScan();
    if (vg && image) nvgDeleteImage(vg, image);
}

bool openImageEntry(const SmbEntry& entry, int source, NVGcontext* vg, bool preserveHud) {
    if (entry.directory || !isImageFile(entry.name)) return false;
    int hudVisible = 1;
    if (preserveHud) {
        lockScan();
        hudVisible = gImage.hudVisible;
        unlockScan();
    }

    if (entry.size > kMaxImageBytes) {
        char msg[160];
        const unsigned long mb = static_cast<unsigned long>((entry.size + 1024 * 1024 - 1) / (1024 * 1024));
        std::snprintf(msg, sizeof(msg), t("image.too_large.fmt"), mb);
        setImageError(entry.name, msg);
        return true;
    }

    unsigned char* data = nullptr;
    int dataSize = 0;
    char path[320];
    if (source == SourceLocal) {
        buildLocalFilePath(entry.name, path, sizeof(path));
        if (!readLocalFileToMemory(path, entry.size, &data, &dataSize)) {
            setImageError(entry.name, t("image.read_failed"));
            return true;
        }
    } else {
        buildSmbFilePath(entry.name, path, sizeof(path));
        if (!readSmbFileToMemory(path, entry.size, &data, &dataSize)) {
            setImageError(entry.name, t("image.read_failed"));
            return true;
        }
    }

    const int image = nvgCreateImageMem(vg, 0, data, dataSize);
    std::free(data);
    if (!image) {
        setImageError(entry.name, t("image.decode_failed"));
        return true;
    }

    int w = 0;
    int h = 0;
    nvgImageSize(vg, image, &w, &h);

    closeImage(vg);
    lockScan();
    gImage.nvgImage = image;
    gImage.loaded = 1;
    gImage.error = 0;
    gImage.width = w;
    gImage.height = h;
    gImage.rotationDegrees = 0;
    gImage.hudVisible = preserveHud ? hudVisible : 1;
    gImage.zoom = fitImageScale(w, h, 0);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    copyText(gImage.fileName, sizeof(gImage.fileName), entry.name);
    std::snprintf(gImage.message, sizeof(gImage.message), "%dx%d", w, h);
    unlockScan();
    return true;
}

void drawImageViewer(NVGcontext* vg, int font, const ImageState& image) {
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
    nvgFillColor(vg, nvgRGB(6, 8, 11));
    nvgFill(vg);

    if (image.loaded && image.nvgImage) {
        nvgSave(vg);
        nvgTranslate(vg, kWidth * 0.5f + image.offsetX, kHeight * 0.5f + image.offsetY);
        nvgRotate(vg, (static_cast<float>(image.rotationDegrees) * kPi) / 180.0f);
        const float w = static_cast<float>(image.width) * image.zoom;
        const float h = static_cast<float>(image.height) * image.zoom;
        NVGpaint img = nvgImagePattern(vg, -w * 0.5f, -h * 0.5f, w, h, 0.0f, image.nvgImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, -w * 0.5f, -h * 0.5f, w, h);
        nvgFillPaint(vg, img);
        nvgFill(vg);
        nvgRestore(vg);
    } else {
        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 24.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, image.error ? nvgRGB(245, 104, 104) : nvgRGB(190, 202, 210));
        nvgText(vg, kWidth * 0.5f, kHeight * 0.5f - 16.0f,
                image.message[0] ? image.message : t("image.loading"), nullptr);
    }

    if (image.loaded && !image.hudVisible) return;

    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, 70.0f,
                                           nvgRGBA(0, 0, 0, 170), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, 70.0f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    drawMarqueeText(vg, font, 40.0f, 34.0f, 650.0f, 20.0f,
                    nvgRGB(245, 250, 255), image.fileName[0] ? image.fileName : t("app.title"), true);

    char info[96];
    if (image.loaded) {
        std::snprintf(info, sizeof(info), "%dx%d  %d%%",
                      image.width, image.height, static_cast<int>(image.zoom * 100.0f + 0.5f));
    } else {
        copyText(info, sizeof(info), image.error ? t("image.open_failed") : t("image.loading"));
    }
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGB(178, 194, 204));
    nvgText(vg, 920.0f, 34.0f, info, nullptr);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 500.0f, kWidth, 44.0f);
    nvgFillColor(vg, nvgRGB(10, 14, 18));
    nvgFill(vg);

    const FooterHint hints[] = {
        {"L/R", t("hint.prev_next")},
        {"左搖桿", t("hint.move")},
        {"↑↓/右搖桿", t("hint.zoom")},
        {"○", t("hint.reset")},
        {"△", t("hint.rotate")},
        {"□", t("hint.hud")},
        {"×", t("hint.back")},
    };
    drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
}

void zoomImage(float factor) {
    if (factor <= 0.0f) return;
    lockScan();
    const float fit = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    const float minZoom = fit * 0.25f;
    const float maxZoom = fit * 12.0f > 8.0f ? fit * 12.0f : 8.0f;
    gImage.zoom *= factor;
    if (gImage.zoom < minZoom) gImage.zoom = minZoom;
    if (gImage.zoom > maxZoom) gImage.zoom = maxZoom;
    clampImageViewLocked();
    unlockScan();
}

void panImage(float dx, float dy) {
    lockScan();
    gImage.offsetX += dx;
    gImage.offsetY += dy;
    clampImageViewLocked();
    unlockScan();
}

void rotateImageClockwise() {
    lockScan();
    gImage.rotationDegrees = (gImage.rotationDegrees + 90) % 360;
    gImage.zoom = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    clampImageViewLocked();
    unlockScan();
}

void toggleImageHud() {
    lockScan();
    gImage.hudVisible = !gImage.hudVisible;
    unlockScan();
}

int findAdjacentImageIndex(const ScanState& scan, int selected, int direction) {
    if (scan.phase != ScanReady || scan.count <= 0 || direction == 0) return -1;
    int i = selected;
    for (int step = 0; step < scan.count; ++step) {
        i += direction;
        if (i < 0 || i >= scan.count) return -1;
        if (!scan.entries[i].directory && isImageFile(scan.entries[i].name)) return i;
    }
    return -1;
}
