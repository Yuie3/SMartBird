#include "UI/Widgets.hpp"

#include "Config/I18n.hpp"
#include "Core/State.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Math.hpp"

#include <cstdio>

void drawText(NVGcontext* vg, int font, float x, float y, float size, NVGcolor color, const char* text) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, text, nullptr);
}

float textWidth(NVGcontext* vg, int font, float size, const char* text) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    float bounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, text ? text : "", nullptr, bounds);
    return bounds[2] - bounds[0];
}

void drawMarqueeText(NVGcontext* vg, int font, float x, float y, float maxW, float size,
                     NVGcolor color, const char* text, bool active) {
    if (!text || !text[0] || maxW <= 0.0f) return;

    const float width = textWidth(vg, font, size, text);
    float offset = 0.0f;
    if (active && width > maxW) {
        const float overflow = width - maxW;
        const float gap = 56.0f;
        const float speed = 0.65f;
        const int pauseFrames = 75;
        const int travelFrames = static_cast<int>((overflow + gap) / speed) + 1;
        const int cycle = pauseFrames + travelFrames;
        const int phase = cycle > 0 ? gUiFrame % cycle : 0;
        if (phase >= pauseFrames) {
            offset = (phase - pauseFrames) * speed;
            if (offset > overflow + gap) offset = overflow + gap;
        }
    }

    nvgSave(vg);
    nvgIntersectScissor(vg, x, y - size - 6.0f, maxW, size + 12.0f);
    drawText(vg, font, x - offset, y, size, color, text);
    nvgRestore(vg);
}

void drawFolderGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 4.0f, 30.0f, 20.0f, 3.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 2.0f, y, 13.0f, 8.0f, 2.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPlayGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgCircle(vg, x + 14.0f, y + 14.0f, 14.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 28));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 10.0f, y + 7.0f);
    nvgLineTo(vg, x + 21.0f, y + 14.0f);
    nvgLineTo(vg, x + 10.0f, y + 21.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPhotoGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 2.0f, 30.0f, 24.0f, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 24));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.4f);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x + 22.0f, y + 9.0f, 3.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 5.0f, y + 22.0f);
    nvgLineTo(vg, x + 13.0f, y + 13.0f);
    nvgLineTo(vg, x + 19.0f, y + 19.0f);
    nvgLineTo(vg, x + 25.0f, y + 14.0f);
    nvgLineTo(vg, x + 29.0f, y + 22.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawSmallBadge(NVGcontext* vg, int font, float x, float y, float w,
                    const char* label, NVGcolor bg, NVGcolor fg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, 22.0f, 3.0f);
    nvgFillColor(vg, bg);
    nvgFill(vg);
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, fg);
    nvgText(vg, x + w * 0.5f, y + 11.0f, label, nullptr);
}

void drawEntryRow(NVGcontext* vg, int font, const SmbEntry& entry, int row, bool selected) {
    const float top = 178.0f + row * 38.0f;
    const float y = top + 26.0f;
    const float rowX = 40.0f;
    const float rowW = 880.0f;
    if (!selected && (row % 2) == 1) {
        nvgBeginPath(vg);
        nvgRect(vg, rowX, top, rowW, 36.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 10));
        nvgFill(vg);
    }
    if (selected) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, rowX, top, rowW, 36.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(30, 76, 99));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, rowX, top, 5.0f, 36.0f, 2.0f);
        nvgFillColor(vg, nvgRGB(0, 180, 216));
        nvgFill(vg);
    }

    const NVGcolor textColor = selected ? nvgRGB(250, 253, 255) : nvgRGB(219, 228, 232);
    if (entry.directory) {
        drawFolderGlyph(vg, 58.0f, top + 7.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(118, 177, 196));
        drawMarqueeText(vg, font, 108.0f, y, 800.0f, 21.0f, textColor, entry.name, selected);
    } else {
        const unsigned long mb = static_cast<unsigned long>((entry.size + 1024 * 1024 - 1) / (1024 * 1024));
        char sizeLine[40];
        std::snprintf(sizeLine, sizeof(sizeLine), "%lu MB", mb);
        if (isImageFile(entry.name)) {
            drawPhotoGlyph(vg, 58.0f, top + 4.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(156, 188, 201));
        } else {
            drawPlayGlyph(vg, 58.0f, top + 4.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(156, 188, 201));
        }
        drawSmallBadge(vg, font, 98.0f, top + 7.0f, 52.0f, fileExtensionLabel(entry.name),
                       selected ? nvgRGBA(0, 180, 216, 70) : nvgRGBA(255, 255, 255, 22),
                       selected ? nvgRGB(225, 248, 255) : nvgRGB(172, 190, 200));

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
        nvgFillColor(vg, textColor);
        nvgText(vg, 908.0f, y, sizeLine, nullptr);

        drawMarqueeText(vg, font, 168.0f, y, 620.0f, 21.0f, textColor, entry.name, selected);
    }
}

void drawHiddenRow(NVGcontext* vg, int font, const HiddenItem& item, int row, bool selected) {
    const float top = 178.0f + row * 38.0f;
    const float y = top + 26.0f;
    if (!selected && (row % 2) == 1) {
        nvgBeginPath(vg);
        nvgRect(vg, 40.0f, top, 880.0f, 36.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 10));
        nvgFill(vg);
    }
    if (selected) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 40.0f, top, 880.0f, 36.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(30, 76, 99));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 40.0f, top, 5.0f, 36.0f, 2.0f);
        nvgFillColor(vg, nvgRGB(0, 180, 216));
        nvgFill(vg);
    }

    const NVGcolor textColor = selected ? nvgRGB(250, 253, 255) : nvgRGB(219, 228, 232);
    char detail[384];
    if (item.source == SourceSmb) {
        drawText(vg, font, 56.0f, y, 19.0f, textColor, "SMB");
        std::snprintf(detail, sizeof(detail), "//%s/%s/%s   %s",
                      item.server, item.share, item.path, item.name);
    } else {
        drawText(vg, font, 56.0f, y, 19.0f, textColor, "LOCAL");
        std::snprintf(detail, sizeof(detail), "%s   %s", item.path, item.name);
    }
    drawMarqueeText(vg, font, 136.0f, y, 772.0f, 19.0f, textColor, detail, selected);
}

void drawCopyProgress(NVGcontext* vg, int font, const CopyState& copy) {
    if (!copy.busy && !copy.done && !copy.message[0]) return;

    const float x = 40.0f;
    const float y = 146.0f;
    const float w = 880.0f;
    const float h = 22.0f;
    float frac = 0.0f;
    if (copy.total > 0) {
        frac = static_cast<float>(static_cast<double>(copy.copied) / static_cast<double>(copy.total));
        frac = clampFloat(frac, 0.0f, 1.0f);
    }

    char line[256];
    const int percent = static_cast<int>(frac * 100.0f + 0.5f);
    if (copy.busy) {
        std::snprintf(line, sizeof(line), t("copy.busy.fmt"), percent, copy.fileName);
    } else if (copy.error) {
        std::snprintf(line, sizeof(line), t("copy.error.fmt"), copy.message);
    } else {
        std::snprintf(line, sizeof(line), t("copy.done.fmt"), copy.destPath);
    }

    drawText(vg, font, x, y, 15.0f,
             copy.error ? nvgRGB(245, 104, 104) : nvgRGB(190, 202, 210), line);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w, h, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 38));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w * (copy.done && !copy.error ? 1.0f : frac), h, 3.0f);
    nvgFillColor(vg, copy.error ? nvgRGB(245, 104, 104) : nvgRGB(0, 180, 216));
    nvgFill(vg);
}

void drawConnectRow(NVGcontext* vg, int font, float y, const char* label, const char* value, bool focused) {
    const float x = 58.0f;
    const float w = 844.0f;
    const float h = 36.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y - 25.0f, w, h, 4.0f);
    nvgFillColor(vg, focused ? nvgRGB(36, 89, 116) : nvgRGB(20, 27, 34));
    nvgFill(vg);

    drawText(vg, font, x + 16.0f, y, 18.0f,
             focused ? nvgRGB(245, 250, 255) : nvgRGB(160, 174, 184), label);
    drawText(vg, font, x + 185.0f, y, 18.0f,
             focused ? nvgRGB(250, 253, 255) : nvgRGB(218, 226, 232),
             value && value[0] ? value : t("field.empty"));
}

void drawConnectActionButton(NVGcontext* vg, int font, float x, float y, float w,
                             const char* button, const char* label, bool focused) {
    const float h = 68.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, 5.0f);
    nvgFillColor(vg, focused ? nvgRGB(38, 94, 122) : nvgRGB(20, 27, 34));
    nvgFill(vg);

    if (focused) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 4.0f);
        nvgStrokeWidth(vg, 1.5f);
        nvgStrokeColor(vg, nvgRGB(92, 186, 219));
        nvgStroke(vg);
    }

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
    nvgFontSize(vg, 15.0f);
    nvgFillColor(vg, focused ? nvgRGB(184, 235, 250) : nvgRGB(145, 164, 176));
    nvgText(vg, x + w * 0.5f, y + 25.0f, button, nullptr);

    nvgFontSize(vg, 19.0f);
    nvgFillColor(vg, focused ? nvgRGB(250, 253, 255) : nvgRGB(218, 226, 232));
    nvgText(vg, x + w * 0.5f, y + 51.0f, label, nullptr);
}

void drawFooterHints(NVGcontext* vg, int font, const FooterHint* hints, int count) {
    float x = 40.0f;
    const float y = 512.0f;
    for (int i = 0; i < count; ++i) {
        if (!hints[i].key || !hints[i].label) continue;
        const float keyW = textWidth(vg, font, 14.0f, hints[i].key) + 20.0f;
        const float labelW = textWidth(vg, font, 16.0f, hints[i].label);
        const float itemW = keyW + labelW + 20.0f;
        if (x + itemW > 920.0f) break;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, keyW, 22.0f, 4.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 22));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 36));
        nvgStroke(vg);

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 14.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(225, 234, 238));
        nvgText(vg, x + keyW * 0.5f, y + 11.0f, hints[i].key, nullptr);

        nvgFontSize(vg, 16.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(178, 194, 204));
        nvgText(vg, x + keyW + 7.0f, y + 11.0f, hints[i].label, nullptr);
        x += itemW;
    }
}
