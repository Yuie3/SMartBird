#include "UI/Screens.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/I18n.hpp"
#include "Config/Settings.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Image/ImageViewer.hpp"
#include "Player/PlayerOverlay.hpp"
#include "Player/VideoPlayer.hpp"
#include "UI/ImeInput.hpp"
#include "UI/Widgets.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Text.hpp"
#include "nanovg.h"

#include <cstdio>
#include <cstring>

namespace {

void maskPassword(const char* src, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    size_t len = src ? std::strlen(src) : 0;
    if (len >= outSize) len = outSize - 1;
    for (size_t i = 0; i < len; ++i) out[i] = '*';
    out[len] = '\0';
}

} // namespace

void renderUi(NVGcontext* vg, int font, const RuntimeStatus& runtime, const ScanState& scan,
              const PlayerState& player, const ImageState& image,
              AppMode mode, int selected, int listTop, int connectFocus) {
    if (mode == ModePlayer) {
        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);

        if (player.errorTitle[0]) {
            drawText(vg, font, 80.0f, 150.0f, 34.0f, nvgRGB(245, 104, 104), player.errorTitle);
            drawText(vg, font, 80.0f, 205.0f, 22.0f, nvgRGB(235, 238, 244), player.fileName);
            drawText(vg, font, 80.0f, 260.0f, 20.0f, nvgRGB(190, 202, 210), player.errorReason);
            drawText(vg, font, 80.0f, 340.0f, 18.0f, nvgRGB(150, 164, 172),
                     std::strcmp(player.errorTitle, t("error.resolution.title")) == 0
                         ? t("error.resolution.hint")
                         : t("error.convert.hint"));
            drawText(vg, font, 80.0f, 500.0f, 17.0f, nvgRGB(210, 218, 224), t("player.back"));
            return;
        }

        if (player.hasFrame && gPlayer.nvgImage) {
            drawVideo(vg, 0.0f, 0.0f, kWidth, kHeight);
        } else {
            nvgFontFaceId(vg, font);
            nvgFontSize(vg, 22.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, nvgRGB(150, 155, 175));
            nvgText(vg, kWidth * 0.5f, kHeight * 0.5f,
                    player.loading ? t("player.loading") : t("player.waiting"), nullptr);
        }

        if (player.loading || player.paused || player.overlayFrames > 0 || !player.hasFrame) {
            if (player.rotationDegrees == 270) {
                nvgSave(vg);
                nvgTranslate(vg, 0.0f, kHeight);
                nvgRotate(vg, -0.5f * kPi);
                drawPlayerOverlay(vg, font, player, kHeight, kWidth);
                nvgRestore(vg);
            } else {
                drawPlayerOverlay(vg, font, player, kWidth, kHeight);
            }
        }
        return;
    }

    if (mode == ModeImage) {
        drawImageViewer(vg, font, image);
        return;
    }

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
    nvgFillColor(vg, nvgRGB(12, 16, 21));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, 124.0f);
    nvgFillColor(vg, nvgRGB(22, 29, 36));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 500.0f, kWidth, 44.0f);
    nvgFillColor(vg, nvgRGB(16, 21, 27));
    nvgFill(vg);

    if (mode == ModeConnect) {
        drawText(vg, font, 58.0f, 66.0f, 38.0f, nvgRGB(240, 245, 248), t("app.title"));
        drawText(vg, font, 60.0f, 108.0f, 20.0f, nvgRGB(170, 185, 194), t("login.title"));

        char passwordMasked[64];
        maskPassword(gConn.password, passwordMasked, sizeof(passwordMasked));
        drawConnectRow(vg, font, 158.0f, connectFieldLabel(ConnectServer), gConn.server, connectFocus == ConnectServer);
        drawConnectRow(vg, font, 194.0f, connectFieldLabel(ConnectShare), gConn.share, connectFocus == ConnectShare);
        drawConnectRow(vg, font, 230.0f, connectFieldLabel(ConnectPath), gConn.path, connectFocus == ConnectPath);
        drawConnectRow(vg, font, 266.0f, connectFieldLabel(ConnectUser), gConn.user, connectFocus == ConnectUser);
        drawConnectRow(vg, font, 302.0f, connectFieldLabel(ConnectPassword), passwordMasked, connectFocus == ConnectPassword);
        drawConnectRow(vg, font, 338.0f, connectFieldLabel(ConnectDomain), gConn.domain, connectFocus == ConnectDomain);
        drawConnectRow(vg, font, 374.0f, connectFieldLabel(ConnectLocalPath), gConn.localPath, connectFocus == ConnectLocalPath);
        drawConnectActionButton(vg, font, 58.0f, 408.0f, 268.0f,
                                "START", t("connect.smb_button"), connectFocus == ConnectSmbAction);
        drawConnectActionButton(vg, font, 346.0f, 408.0f, 268.0f,
                                "SELECT", t("connect.local_button"), connectFocus == ConnectLocalAction);
        drawConnectActionButton(vg, font, 634.0f, 408.0f, 268.0f,
                                "L", t("connect.hidden_button"), connectFocus == ConnectHiddenAction);

        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", t("hint.edit")},
            {"×", t("hint.exit")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    if (mode == ModeHidden) {
        drawText(vg, font, 40.0f, 58.0f, 34.0f, nvgRGB(240, 245, 248), t("hidden.title"));

        char countLine[128];
        std::snprintf(countLine, sizeof(countLine), t("hidden.count.fmt"), gHiddenItemCount);
        drawText(vg, font, 40.0f, 94.0f, 18.0f, nvgRGB(170, 185, 194), countLine);
        drawText(vg, font, 40.0f, 132.0f, 18.0f, nvgRGB(119, 220, 153), t("hidden.hint"));

        if (gHiddenItemCount > 0) {
            int first = listTop;
            if (first > gHiddenItemCount - kVisibleEntries) first = gHiddenItemCount - kVisibleEntries;
            if (first < 0) first = 0;
            const int last = (gHiddenItemCount - first) < kVisibleEntries
                ? gHiddenItemCount
                : first + kVisibleEntries;
            for (int i = first; i < last; ++i) {
                drawHiddenRow(vg, font, gHiddenItems[i], i - first, i == selected);
            }
        } else {
            drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("hidden.empty"));
        }

        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", t("hint.unhide")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    drawText(vg, font, 40.0f, 58.0f, 34.0f, nvgRGB(240, 245, 248), t("app.title"));

    char shareLine[256];
    if (scan.source == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        std::snprintf(shareLine, sizeof(shareLine), t("local.prefix.fmt"), scan.path[0] ? scan.path : localRoot);
    } else {
        std::snprintf(shareLine, sizeof(shareLine), t("share.prefix.fmt"), gConn.server, gConn.share,
                      scan.path[0] ? scan.path : "");
    }
    drawText(vg, font, 40.0f, 94.0f, 18.0f, nvgRGB(170, 185, 194), shareLine);

    char libLine[128];
    if (scan.source == SourceLocal) {
        std::snprintf(libLine, sizeof(libLine), t("runtime.local.fmt"), runtime.mpvClientApi);
    } else {
        std::snprintf(libLine, sizeof(libLine), t("runtime.smb.fmt"),
                      runtime.smb2ContextOk ? "OK" : t("runtime.failed"), runtime.mpvClientApi);
    }
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGB(92, 107, 118));
    nvgText(vg, 920.0f, 62.0f, libLine, nullptr);

    NVGcolor statusColor = nvgRGB(171, 185, 194);
    if (scan.phase == ScanReady) statusColor = nvgRGB(119, 220, 153);
    if (scan.phase == ScanError) statusColor = nvgRGB(245, 104, 104);
    drawText(vg, font, 40.0f, 132.0f, 18.0f, statusColor, scan.message);

    CopyState copy = {};
    lockScan();
    copy = gCopyState;
    unlockScan();
    drawCopyProgress(vg, font, copy);

    if (scan.phase == ScanReady && scan.count > 0) {
        int first = listTop;
        if (first > scan.count - kVisibleEntries) first = scan.count - kVisibleEntries;
        if (first < 0) first = 0;
        const int last = (scan.count - first) < kVisibleEntries ? scan.count : first + kVisibleEntries;
        for (int i = first; i < last; ++i) {
            drawEntryRow(vg, font, scan.entries[i], i - first, i == selected);
        }
    } else if (scan.phase == ScanReady) {
        drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("browser.empty"));
    } else if (scan.phase == ScanLoading) {
        drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("browser.wait"));
    }

    const bool hasSelectedEntry = scan.phase == ScanReady && selected >= 0 && selected < scan.count;
    const bool selectedDir = hasSelectedEntry && scan.entries[selected].directory;
    const bool selectedImage = hasSelectedEntry && !selectedDir && isImageFile(scan.entries[selected].name);
    if (!hasSelectedEntry) {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else if (scan.source == SourceSmb && !selectedDir) {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", selectedImage ? t("hint.view") : t("hint.play")},
            {"□", t("hint.copy")},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", selectedDir ? t("hint.enter") : (selectedImage ? t("hint.view") : t("hint.play"))},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    }
}
