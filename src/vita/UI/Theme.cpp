#include "UI/Theme.hpp"

#include "Core/Constants.hpp"

namespace ui {

NVGcolor textPrimary() { return nvgRGB(246, 253, 255); }
NVGcolor textSecondary() { return nvgRGBA(216, 240, 248, 230); }
NVGcolor textMuted() { return nvgRGBA(164, 196, 210, 220); }
NVGcolor accent() { return nvgRGB(79, 218, 244); }
NVGcolor accentSoft() { return nvgRGBA(111, 221, 245, 150); }
NVGcolor danger() { return nvgRGB(255, 116, 126); }

void drawAppBackground(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
    const NVGpaint bg = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, kHeight,
                                          nvgRGB(42, 169, 213), nvgRGB(8, 53, 104));
    nvgFillPaint(vg, bg);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kTopBarHeight);
    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, kTopBarHeight,
                                           nvgRGBA(255, 255, 255, 74), nvgRGBA(255, 255, 255, 10));
    nvgFillPaint(vg, top);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, 0.0f, 128.0f);
    nvgLineTo(vg, kWidth, 92.0f);
    nvgLineTo(vg, kWidth, 178.0f);
    nvgLineTo(vg, 0.0f, 218.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 18));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, 0.0f, 420.0f);
    nvgLineTo(vg, kWidth, 360.0f);
    nvgLineTo(vg, kWidth, kHeight);
    nvgLineTo(vg, 0.0f, kHeight);
    nvgClosePath(vg);
    nvgFillColor(vg, nvgRGBA(0, 18, 48, 58));
    nvgFill(vg);

    drawFooterBar(vg);
}

void drawTopBar(NVGcontext* vg, int font, const char* title, const char* subtitle) {
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, kTopBarHeight - 1.0f, kWidth, 1.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 54));
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFontSize(vg, 23.0f);
    nvgFillColor(vg, textPrimary());
    nvgSave(vg);
    nvgIntersectScissor(vg, kPageMargin + 8.0f, 10.0f, kWidth - (kPageMargin + 8.0f) * 2.0f, 34.0f);
    nvgText(vg, kPageMargin + 8.0f, 38.0f, title ? title : "", nullptr);
    nvgRestore(vg);

    if (subtitle && subtitle[0]) {
        nvgFontSize(vg, 15.0f);
        nvgFillColor(vg, textSecondary());
        nvgSave(vg);
        nvgIntersectScissor(vg, kPageMargin + 8.0f, 47.0f, kWidth - (kPageMargin + 8.0f) * 2.0f, 24.0f);
        nvgText(vg, kPageMargin + 8.0f, 64.0f, subtitle, nullptr);
        nvgRestore(vg);
    }
}

void drawFooterBar(NVGcontext* vg) {
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, kFooterTop, kWidth, kFooterHeight);
    nvgFillColor(vg, nvgRGBA(3, 31, 68, 158));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, kFooterTop, kWidth, 1.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 58));
    nvgFill(vg);
}

void drawGlassPanel(NVGcontext* vg, float x, float y, float w, float h, float radius, bool strong) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillColor(vg, strong ? nvgRGBA(4, 46, 95, 170) : nvgRGBA(5, 58, 112, 118));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, strong ? 88 : 50));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h * 0.36f, radius - 1.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, strong ? 28 : 16));
    nvgFill(vg);
}

void drawFocusPanel(NVGcontext* vg, float x, float y, float w, float h, float radius, float pulse) {
    if (pulse < 0.0f) pulse = 0.0f;
    if (pulse > 1.0f) pulse = 1.0f;
    x -= pulse * 2.0f;
    y -= pulse * 1.5f;
    w += pulse * 4.0f;
    h += pulse * 3.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x - 1.0f, y - 1.0f, w + 2.0f, h + 2.0f, radius);
    nvgFillColor(vg, nvgRGBA(219, 252, 255, static_cast<unsigned char>(82 + pulse * 52.0f)));
    nvgFill(vg);

    const NVGpaint fill = nvgLinearGradient(vg, x, y, x, y + h,
                                            nvgRGBA(113, 228, 248, 220),
                                            nvgRGBA(31, 136, 207, 224));
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, radius);
    nvgFillPaint(vg, fill);
    nvgFill(vg);

    nvgStrokeWidth(vg, 1.4f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 210));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 8.0f, y + 9.0f, 5.0f, h - 18.0f, 2.5f);
    nvgFillColor(vg, nvgRGB(246, 254, 255));
    nvgFill(vg);

    if (pulse > 0.0f) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, radius - 1.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, static_cast<unsigned char>(pulse * 48.0f)));
        nvgFill(vg);
    }
}

void drawKeyCapsule(NVGcontext* vg, int font, float x, float y, const char* key, bool active) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    float bounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, key ? key : "", nullptr, bounds);
    const float w = bounds[2] - bounds[0] + 18.0f;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, 22.0f, 5.0f);
    nvgFillColor(vg, active ? nvgRGBA(106, 224, 246, 150) : nvgRGBA(0, 36, 82, 105));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, active ? 128 : 70));
    nvgStroke(vg);

    nvgFillColor(vg, textPrimary());
    nvgText(vg, x + w * 0.5f, y + 11.0f, key ? key : "", nullptr);
}

} // namespace ui
