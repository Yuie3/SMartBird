#include "Player/PlayerOverlay.hpp"

#include "Config/I18n.hpp"
#include "Core/Constants.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Math.hpp"
#include "nanovg.h"

#include <cstdio>

namespace {

void formatTime(double seconds, char* out, size_t outSize) {
    if (seconds <= 0.0) {
        std::snprintf(out, outSize, "0:00");
        return;
    }
    const int total = static_cast<int>(seconds + 0.5);
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int secs = total % 60;
    if (hours > 0) std::snprintf(out, outSize, "%d:%02d:%02d", hours, minutes, secs);
    else std::snprintf(out, outSize, "%d:%02d", minutes, secs);
}

void drawSkipButton(NVGcontext* vg, int font, float cx, float cy, float r, bool forward, NVGcolor color) {
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 3.0f);
    nvgBeginPath(vg);
    if (forward) nvgArc(vg, cx, cy, r, 1.5f * kPi, 0.0f, NVG_CCW);
    else nvgArc(vg, cx, cy, r, -0.5f * kPi, kPi, NVG_CW);
    nvgStroke(vg);

    const float ty = cy - r;
    nvgBeginPath(vg);
    if (forward) {
        nvgMoveTo(vg, cx - 6.0f, ty - 8.0f);
        nvgLineTo(vg, cx + 9.0f, ty);
        nvgLineTo(vg, cx - 6.0f, ty + 8.0f);
    } else {
        nvgMoveTo(vg, cx + 6.0f, ty - 8.0f);
        nvgLineTo(vg, cx - 9.0f, ty);
        nvgLineTo(vg, cx + 6.0f, ty + 8.0f);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color);
    nvgText(vg, cx, cy + 2.0f, "10", nullptr);
}

void drawPlayPauseDisc(NVGcontext* vg, float cx, float cy, float r, bool paused) {
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    if (paused) {
        nvgMoveTo(vg, cx - 7.0f, cy - 12.0f);
        nvgLineTo(vg, cx + 13.0f, cy);
        nvgLineTo(vg, cx - 7.0f, cy + 12.0f);
        nvgClosePath(vg);
        nvgPathWinding(vg, NVG_HOLE);
    } else {
        nvgRect(vg, cx - 8.0f, cy - 11.0f, 5.5f, 22.0f);
        nvgPathWinding(vg, NVG_HOLE);
        nvgRect(vg, cx + 2.5f, cy - 11.0f, 5.5f, 22.0f);
        nvgPathWinding(vg, NVG_HOLE);
    }
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 210));
    nvgFill(vg);
}

} // namespace

void drawPlayerOverlay(NVGcontext* vg, int font, const PlayerState& player, float viewW, float viewH) {
    const NVGcolor white = nvgRGB(245, 245, 245);

    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, 58.0f,
                                           nvgRGBA(0, 0, 0, 160), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, viewW, 58.0f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    const NVGpaint bottom = nvgLinearGradient(vg, 0.0f, viewH - 118.0f, 0.0f, viewH,
                                              nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 180));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, viewH - 118.0f, viewW, 118.0f);
    nvgFillPaint(vg, bottom);
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, 16.0f, 23.0f, "←", nullptr);

    drawMarqueeText(vg, font, 48.0f, 29.0f, viewW - 285.0f, 18.0f,
                    white, player.fileName[0] ? player.fileName : t("app.title"), true);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 190));
    char rotateLine[40];
    std::snprintf(rotateLine, sizeof(rotateLine), t("player.rotate.fmt"), player.rotationDegrees == 270 ? 0 : 270);
    nvgText(vg, viewW - 118.0f, 23.0f, rotateLine, nullptr);
    nvgText(vg, viewW - 16.0f, 23.0f, t("player.back"), nullptr);

    const float transportY = viewH - 82.0f;
    const float playX = viewW - 44.0f;
    const float forwardX = viewW - 118.0f;
    const float backX = viewW - 192.0f;
    const NVGcolor button = nvgRGBA(255, 255, 255, 210);
    drawSkipButton(vg, font, backX, transportY, 17.0f, false, button);
    drawSkipButton(vg, font, forwardX, transportY, 17.0f, true, button);
    drawPlayPauseDisc(vg, playX, transportY, 26.0f, player.paused);

    char pos[16], dur[16], timeLine[40];
    formatTime(player.positionSeconds, pos, sizeof(pos));
    formatTime(player.durationSeconds, dur, sizeof(dur));
    std::snprintf(timeLine, sizeof(timeLine), "%s / %s", pos, dur);

    const float midY = viewH - 24.0f;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 18.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, 132.0f, midY, timeLine, nullptr);

    const float barX = 250.0f;
    const float barY = midY;
    const float barW = viewW - barX - 42.0f;
    float frac = 0.0f;
    if (player.durationSeconds > 0.0) {
        frac = static_cast<float>(player.positionSeconds / player.durationSeconds);
        frac = clampFloat(frac, 0.0f, 1.0f);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - 3.0f, barW, 6.0f, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 60));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - 3.0f, barW * frac, 6.0f, 3.0f);
    nvgFillColor(vg, nvgRGB(0, 180, 216));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, barX + barW * frac, barY, 7.0f);
    nvgFillColor(vg, nvgRGB(0, 180, 216));
    nvgFill(vg);
}
