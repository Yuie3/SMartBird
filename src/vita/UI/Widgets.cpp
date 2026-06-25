#include "UI/Widgets.hpp"

#include "Config/I18n.hpp"
#include "Core/State.hpp"
#include "UI/Theme.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Math.hpp"

#include <cstdio>

namespace {

void drawGlossyPanel(NVGcontext* vg, float x, float y, float w, float h, bool selected) {
    if (selected) {
        ui::drawFocusPanel(vg, x, y, w, h, ui::kPanelRadius);
    } else {
        ui::drawGlassPanel(vg, x, y, w, h, ui::kPanelRadius, false);
    }
}

void drawChevronGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgMoveTo(vg, x, y);
    nvgLineTo(vg, x + 17.0f, y + 11.0f);
    nvgLineTo(vg, x, y + 22.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

} // namespace

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
        const float gap = 56.0f;
        const float speed = 0.48f;
        const int pauseFrames = 55;
        const int travelFrames = static_cast<int>((width + gap) / speed) + 1;
        const int cycle = pauseFrames + travelFrames;
        const int phase = cycle > 0 ? gUiFrame % cycle : 0;
        if (phase >= pauseFrames) {
            offset = (phase - pauseFrames) * speed;
        }
    }

    nvgSave(vg);
    nvgIntersectScissor(vg, x, y - size - 6.0f, maxW, size + 12.0f);
    drawText(vg, font, x - offset, y, size, color, text);
    if (active && width > maxW) {
        drawText(vg, font, x - offset + width + 56.0f, y, size, color, text);
    }
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

void drawListFocus(NVGcontext* vg, float top, float pulse) {
    ui::drawFocusPanel(vg, ui::kPageMargin, top, kWidth - ui::kPageMargin * 2.0f,
                       kListRowHeight, ui::kPanelRadius, pulse);
}

void drawEntryRowAt(NVGcontext* vg, int font, const SmbEntry& entry, float top, bool selected) {
    const float y = top + 35.0f;
    const float rowX = ui::kPageMargin;
    const float rowW = kWidth - ui::kPageMargin * 2.0f;
    if (!selected) {
        drawGlossyPanel(vg, rowX, top, rowW, kListRowHeight, false);
    }

    const NVGcolor textColor = selected ? ui::textPrimary() : ui::textSecondary();
    const NVGcolor iconColor = selected ? ui::textPrimary() : nvgRGBA(190, 232, 246, 225);
    if (entry.directory) {
        drawFolderGlyph(vg, rowX + 28.0f, top + 16.0f, iconColor);
        drawMarqueeText(vg, font, rowX + 80.0f, y, 675.0f, 23.0f, textColor, entry.name, selected);
        drawChevronGlyph(vg, rowX + rowW - 54.0f, top + 16.0f,
                         selected ? ui::textPrimary() : nvgRGBA(166, 204, 218, 210));
    } else {
        const unsigned long mb = static_cast<unsigned long>((entry.size + 1024 * 1024 - 1) / (1024 * 1024));
        char sizeLine[40];
        std::snprintf(sizeLine, sizeof(sizeLine), "%lu MB", mb);
        if (isImageFile(entry.name)) {
            drawPhotoGlyph(vg, rowX + 28.0f, top + 14.0f, iconColor);
        } else {
            drawPlayGlyph(vg, rowX + 28.0f, top + 14.0f, iconColor);
        }
        drawSmallBadge(vg, font, rowX + 80.0f, top + 17.0f, 58.0f, fileExtensionLabel(entry.name),
                       selected ? nvgRGBA(255, 255, 255, 72) : nvgRGBA(1, 40, 88, 120),
                       selected ? ui::textPrimary() : nvgRGBA(216, 242, 250, 230));

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 17.0f);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
        nvgFillColor(vg, selected ? ui::textPrimary() : ui::textMuted());
        nvgText(vg, rowX + rowW - 46.0f, y, sizeLine, nullptr);

        drawMarqueeText(vg, font, rowX + 158.0f, y, 520.0f, 22.0f, textColor, entry.name, selected);
    }
}

void drawEntryRow(NVGcontext* vg, int font, const SmbEntry& entry, int row, bool selected) {
    const float top = kListTopY + row * kListRowPitch;
    if (selected) drawListFocus(vg, top, 0.0f);
    drawEntryRowAt(vg, font, entry, top, selected);
}

void drawHiddenRowAt(NVGcontext* vg, int font, const HiddenItem& item, float top, bool selected) {
    const float y = top + 35.0f;
    const float rowX = ui::kPageMargin;
    const float rowW = kWidth - ui::kPageMargin * 2.0f;
    if (!selected) {
        drawGlossyPanel(vg, rowX, top, rowW, kListRowHeight, false);
    }

    const NVGcolor textColor = selected ? ui::textPrimary() : ui::textSecondary();
    char detail[384];
    if (item.source == SourceSmb) {
        drawSmallBadge(vg, font, rowX + 30.0f, top + 16.0f, 64.0f, "SMB",
                       selected ? nvgRGBA(255, 255, 255, 72) : nvgRGBA(1, 40, 88, 120),
                       textColor);
        std::snprintf(detail, sizeof(detail), "//%s/%s/%s   %s",
                      item.server, item.share, item.path, item.name);
    } else {
        drawSmallBadge(vg, font, rowX + 30.0f, top + 16.0f, 64.0f, "LOCAL",
                       selected ? nvgRGBA(255, 255, 255, 72) : nvgRGBA(1, 40, 88, 120),
                       textColor);
        std::snprintf(detail, sizeof(detail), "%s   %s", item.path, item.name);
    }
    drawMarqueeText(vg, font, rowX + 112.0f, y, 720.0f, 22.0f, textColor, detail, selected);
}

void drawHiddenRow(NVGcontext* vg, int font, const HiddenItem& item, int row, bool selected) {
    const float top = kListTopY + row * kListRowPitch;
    if (selected) drawListFocus(vg, top, 0.0f);
    drawHiddenRowAt(vg, font, item, top, selected);
}

void drawCopyProgress(NVGcontext* vg, int font, const CopyState& copy) {
    if (!copy.busy && !copy.done && !copy.message[0]) return;

    const float x = 560.0f;
    const float y = 62.0f;
    const float w = 360.0f;
    const float h = 8.0f;
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

    drawText(vg, font, x, y, 13.0f,
             copy.error ? ui::danger() : ui::textSecondary(), line);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w, h, 4.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 38));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w * (copy.done && !copy.error ? 1.0f : frac), h, 4.0f);
    nvgFillColor(vg, copy.error ? ui::danger() : ui::accent());
    nvgFill(vg);
}

void drawConnectRow(NVGcontext* vg, int font, float y, const char* label, const char* value, bool focused) {
    const float x = 80.0f;
    const float w = 800.0f;
    const float h = 34.0f;
    if (!focused) ui::drawGlassPanel(vg, x, y - 24.0f, w, h, 6.0f, false);

    drawText(vg, font, x + 16.0f, y, 18.0f,
             focused ? ui::textPrimary() : ui::textMuted(), label);
    drawMarqueeText(vg, font, x + 176.0f, y, 590.0f, 18.0f,
                    focused ? ui::textPrimary() : ui::textSecondary(),
                    value && value[0] ? value : t("field.empty"), focused);
}

void drawConnectActionButton(NVGcontext* vg, int font, float x, float y, float w,
                             const char* button, const char* label, bool focused) {
    const float h = 68.0f;
    if (!focused) ui::drawGlassPanel(vg, x, y, w, h, ui::kPanelRadius, true);

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
    nvgFontSize(vg, 15.0f);
    nvgFillColor(vg, focused ? ui::textPrimary() : ui::textMuted());
    nvgText(vg, x + w * 0.5f, y + 25.0f, button, nullptr);

    nvgFontSize(vg, 19.0f);
    nvgFillColor(vg, focused ? ui::textPrimary() : ui::textSecondary());
    nvgText(vg, x + w * 0.5f, y + 51.0f, label, nullptr);
}

void drawFooterHints(NVGcontext* vg, int font, const FooterHint* hints, int count) {
    float x = 40.0f;
    const float y = ui::kFooterTop + 10.0f;
    const int visibleCount = count > 5 ? 5 : count;
    for (int i = 0; i < visibleCount; ++i) {
        if (!hints[i].key || !hints[i].label) continue;
        const float keyW = textWidth(vg, font, 14.0f, hints[i].key) + 20.0f;
        const float labelW = textWidth(vg, font, 16.0f, hints[i].label);
        const float itemW = keyW + labelW + 20.0f;
        if (x + itemW > 920.0f) break;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, keyW, 22.0f, 5.0f);
        nvgFillColor(vg, nvgRGBA(2, 38, 86, 120));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 78));
        nvgStroke(vg);

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 14.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, ui::textPrimary());
        nvgText(vg, x + keyW * 0.5f, y + 11.0f, hints[i].key, nullptr);

        nvgFontSize(vg, 16.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, ui::textSecondary());
        nvgText(vg, x + keyW + 7.0f, y + 11.0f, hints[i].label, nullptr);
        x += itemW;
    }
}
