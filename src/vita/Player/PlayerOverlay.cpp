#include "Player/PlayerOverlay.hpp"

#include "Config/I18n.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Player/PlayerHudLayout.hpp"
#include "UI/Theme.hpp"
#include "UI/Widgets.hpp"
#include "Utils/Math.hpp"
#include "nanovg.h"

#include <cstdio>
#include <cstring>

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

void formatSeekOffset(double seconds, char* out, size_t outSize) {
    const bool negative = seconds < 0.0;
    double absSeconds = negative ? -seconds : seconds;
    const int rounded = static_cast<int>(absSeconds + 0.5);
    if (rounded < 60) {
        std::snprintf(out, outSize, t("player.swipe_seek.seconds.fmt"), negative ? -rounded : rounded);
        return;
    }

    const int hours = rounded / 3600;
    const int minutes = (rounded / 60) % 60;
    const int secs = rounded % 60;
    if (hours > 0) {
        std::snprintf(out, outSize, "%s%d:%02d:%02d", negative ? "-" : "+", hours, minutes, secs);
    } else {
        std::snprintf(out, outSize, "%s%d:%02d", negative ? "-" : "+", minutes, secs);
    }
}

void drawSwipeSeekOverlay(NVGcontext* vg, int font, const PlayerState& player, float viewW, float viewH) {
    char offset[40];
    char start[24];
    char target[24];
    char range[72];
    formatSeekOffset(player.swipeSeekOffsetSeconds, offset, sizeof(offset));
    formatTime(player.swipeSeekStartSeconds, start, sizeof(start));
    formatTime(player.swipeSeekTargetSeconds, target, sizeof(target));
    std::snprintf(range, sizeof(range), t("player.swipe_seek.time.fmt"), start, target);

    const float w = 286.0f;
    const float h = 126.0f;
    const float x = (viewW - w) * 0.5f;
    const float y = (viewH - h) * 0.5f;
    ui::drawGlassPanel(vg, x, y, w, h, 10.0f, true);

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 17.0f);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, x + w * 0.5f, y + 28.0f, t("player.swipe_seek.title"), nullptr);

    nvgFontSize(vg, 34.0f);
    nvgFillColor(vg, ui::accent());
    nvgText(vg, x + w * 0.5f, y + 66.0f, offset, nullptr);

    nvgFontSize(vg, 18.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, x + w * 0.5f, y + 102.0f, range, nullptr);
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

void drawControlCircle(NVGcontext* vg, float cx, float cy, float r, bool strong) {
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillColor(vg, strong ? nvgRGBA(255, 255, 255, 34) : nvgRGBA(255, 255, 255, 18));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.0f);
    nvgStrokeColor(vg, nvgRGBA(255, 255, 255, strong ? 130 : 62));
    nvgStroke(vg);
}

void drawPlayPauseDisc(NVGcontext* vg, float cx, float cy, float r, bool showPlayIcon) {
    nvgBeginPath(vg);
    if (showPlayIcon) {
        nvgMoveTo(vg, cx - 7.0f, cy - 12.0f);
        nvgLineTo(vg, cx + 13.0f, cy);
        nvgLineTo(vg, cx - 7.0f, cy + 12.0f);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 226));
        nvgFill(vg);
    } else {
        nvgRoundedRect(vg, cx - 9.0f, cy - 12.0f, 6.0f, 24.0f, 1.8f);
        nvgRoundedRect(vg, cx + 3.0f, cy - 12.0f, 6.0f, 24.0f, 1.8f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 226));
        nvgFill(vg);
    }
}

void drawTextButton(NVGcontext* vg, int font, const PlayerHudButton& button) {
    const float x = button.x;
    const float y = button.y + button.h * 0.5f;
    const float w = button.w;
    const char* key = button.key;
    const char* label = button.label;
    const bool active = button.active || gTouchHudAction == static_cast<int>(button.action);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    float keyBounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, key ? key : "", nullptr, keyBounds);
    const float keyW = keyBounds[2] - keyBounds[0];

    if (button.clickable) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y - 14.0f, w, 28.0f, 6.0f);
        nvgFillColor(vg, active ? nvgRGBA(31, 136, 207, 205) : nvgRGBA(5, 58, 112, 172));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, active ? nvgRGBA(220, 252, 255, 150) : nvgRGBA(178, 232, 247, 86));
        nvgStroke(vg);

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 1.0f, y - 13.0f, w - 2.0f, 10.0f, 5.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 18));
        nvgFill(vg);
    }

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, button.clickable ? ui::textMuted() : nvgRGBA(174, 204, 216, 215));
    nvgText(vg, x + 10.0f, y, key, nullptr);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, button.clickable ? ui::textPrimary() : nvgRGBA(220, 238, 244, 225));
    nvgText(vg, x + keyW + 18.0f, y, label, nullptr);
}

void drawHudButton(NVGcontext* vg, int font, const PlayerHudButton& button) {
    const float x = button.x;
    const float y = button.y;
    const float w = button.w;
    const bool active = button.active || gTouchHudAction == static_cast<int>(button.action);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, 28.0f, 6.0f);
    nvgFillColor(vg, active ? nvgRGBA(31, 136, 207, 220) : nvgRGBA(5, 58, 112, 178));
    nvgFill(vg);
    nvgStrokeWidth(vg, active ? 1.25f : 1.0f);
    nvgStrokeColor(vg, active ? nvgRGBA(220, 252, 255, 168) : nvgRGBA(178, 232, 247, 92));
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, 11.0f, 5.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, active ? 34 : 18));
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 14.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, active ? ui::textPrimary() : ui::textSecondary());
    nvgText(vg, x + w * 0.5f, y + button.h * 0.5f, button.label, nullptr);
}

void drawMenuDots(NVGcontext* vg, const PlayerHudButton& button) {
    const bool active = button.active || gTouchHudAction == static_cast<int>(button.action);
    const float cx = button.x + button.w * 0.5f;
    const float cy = button.y + button.h * 0.5f;
    const float r = active ? 1.9f : 1.7f;
    nvgFillColor(vg, active ? ui::textPrimary() : ui::textSecondary());
    for (int i = -1; i <= 1; ++i) {
        nvgBeginPath(vg);
        nvgCircle(vg, cx, cy + i * 6.0f, r);
        nvgFill(vg);
    }
}

void drawTopPlainLabel(NVGcontext* vg, int font, const PlayerHudButton& button) {
    const bool active = button.active || gTouchHudAction == static_cast<int>(button.action);
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, active ? ui::textPrimary() : ui::textSecondary());
    nvgText(vg, button.x + button.w * 0.5f, button.y + button.h * 0.5f,
            button.label, nullptr);
}

void drawBottomIconButton(NVGcontext* vg, const PlayerHudButton& button, const PlayerState& player) {
    const bool active = button.active || gTouchHudAction == static_cast<int>(button.action);
    const float cx = button.x + button.w * 0.5f;
    const float cy = button.y + button.h * 0.5f;
    const NVGcolor color = active ? ui::textPrimary() : ui::textSecondary();

    nvgSave(vg);
    nvgStrokeColor(vg, color);
    nvgFillColor(vg, color);
    nvgStrokeWidth(vg, active ? 2.4f : 2.0f);
    nvgLineCap(vg, NVG_ROUND);
    nvgLineJoin(vg, NVG_ROUND);

    if (button.action == PlayerHudActionShuffle) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 15.0f, cy - 9.0f);
        nvgBezierTo(vg, cx - 4.0f, cy - 9.0f, cx + 2.0f, cy + 9.0f, cx + 15.0f, cy + 9.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 15.0f, cy + 9.0f);
        nvgBezierTo(vg, cx - 4.0f, cy + 9.0f, cx + 2.0f, cy - 9.0f, cx + 15.0f, cy - 9.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 10.0f, cy - 14.0f);
        nvgLineTo(vg, cx + 16.0f, cy - 9.0f);
        nvgLineTo(vg, cx + 10.0f, cy - 4.0f);
        nvgMoveTo(vg, cx + 10.0f, cy + 4.0f);
        nvgLineTo(vg, cx + 16.0f, cy + 9.0f);
        nvgLineTo(vg, cx + 10.0f, cy + 14.0f);
        nvgStroke(vg);
    } else if (button.action == PlayerHudActionPrevious || button.action == PlayerHudActionNext) {
        const bool next = button.action == PlayerHudActionNext;
        const float barX = cx + (next ? 13.0f : -13.0f);
        const float tipX = cx + (next ? 12.0f : -12.0f);
        const float baseX = cx + (next ? -8.0f : 8.0f);
        nvgBeginPath(vg);
        nvgMoveTo(vg, barX, cy - 14.0f);
        nvgLineTo(vg, barX, cy + 14.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, baseX, cy - 13.0f);
        nvgLineTo(vg, tipX, cy);
        nvgLineTo(vg, baseX, cy + 13.0f);
        nvgClosePath(vg);
        nvgFill(vg);
    } else if (button.action == PlayerHudActionPlayPause) {
        if (player.paused || player.loading || !player.hasFrame || player.waitingForValidation) {
            nvgBeginPath(vg);
            nvgMoveTo(vg, cx - 7.0f, cy - 14.0f);
            nvgLineTo(vg, cx + 14.0f, cy);
            nvgLineTo(vg, cx - 7.0f, cy + 14.0f);
            nvgClosePath(vg);
            nvgFill(vg);
        } else {
            nvgBeginPath(vg);
            nvgRoundedRect(vg, cx - 9.0f, cy - 14.0f, 6.0f, 28.0f, 1.5f);
            nvgRoundedRect(vg, cx + 3.0f, cy - 14.0f, 6.0f, 28.0f, 1.5f);
            nvgFill(vg);
        }
    } else if (button.action == PlayerHudActionLoop) {
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx - 15.0f, cy - 8.0f);
        nvgLineTo(vg, cx + 10.0f, cy - 8.0f);
        nvgMoveTo(vg, cx + 5.0f, cy - 13.0f);
        nvgLineTo(vg, cx + 11.0f, cy - 8.0f);
        nvgLineTo(vg, cx + 5.0f, cy - 3.0f);
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, cx + 15.0f, cy + 8.0f);
        nvgLineTo(vg, cx - 10.0f, cy + 8.0f);
        nvgMoveTo(vg, cx - 5.0f, cy + 3.0f);
        nvgLineTo(vg, cx - 11.0f, cy + 8.0f);
        nvgLineTo(vg, cx - 5.0f, cy + 13.0f);
        nvgStroke(vg);
        if (player.repeatMode == PlayerRepeatOne) {
            nvgFontSize(vg, 12.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(vg, cx, cy + 1.0f, "1", nullptr);
        }
    }
    nvgRestore(vg);
}

void drawSpeedSlider(NVGcontext* vg, int font, float x, float y, float w, double speed) {
    const double clamped = speed < 0.5 ? 0.5 : (speed > 2.0 ? 2.0 : speed);
    const float frac = clamped <= 1.0
        ? static_cast<float>((clamped - 0.5) / 0.5 * 0.5)
        : static_cast<float>(0.5 + (clamped - 1.0) / 1.0 * 0.5);
    ui::drawGlassPanel(vg, x, y, w, 44.0f, 6.0f, true);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 18.0f, y + 24.0f, w - 36.0f, 5.0f, 2.5f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 70));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 18.0f, y + 24.0f, (w - 36.0f) * frac, 5.0f, 2.5f);
    nvgFillColor(vg, ui::accent());
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, x + 18.0f + (w - 36.0f) * frac, y + 26.5f, 8.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgFill(vg);

    char label[32];
    std::snprintf(label, sizeof(label), "%.2gx", clamped);
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, x + 18.0f, y + 12.0f, "0.5x", nullptr);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, x + w * 0.5f, y + 12.0f, label, nullptr);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, x + w - 18.0f, y + 12.0f, "2x", nullptr);
}

void drawSettingsBand(NVGcontext* vg, float y, float viewW, float h) {
    const float fadeW = viewW * 0.18f;
    const float solidX = fadeW;
    const float solidW = viewW - fadeW * 2.0f;
    const NVGcolor clear = nvgRGBA(18, 22, 28, 0);
    const NVGcolor fill = nvgRGBA(18, 22, 28, 116);
    const NVGcolor shine = nvgRGBA(255, 255, 255, 44);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, y, fadeW, h);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0.0f, y, fadeW, y, clear, fill));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, solidX, y, solidW, h);
    nvgFillColor(vg, fill);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, viewW - fadeW, y, fadeW, h);
    nvgFillPaint(vg, nvgLinearGradient(vg, viewW - fadeW, y, viewW, y, fill, clear));
    nvgFill(vg);

    const float lineFadeW = fadeW * 1.25f;
    const float lineSolidX = lineFadeW;
    const float lineSolidW = viewW - lineFadeW * 2.0f;
    const NVGcolor shadow = nvgRGBA(0, 0, 0, 48);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, y + 1.0f, lineFadeW, 1.0f);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0.0f, y, lineFadeW, y, nvgRGBA(255, 255, 255, 0), shine));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, lineSolidX, y + 1.0f, lineSolidW, 1.0f);
    nvgFillColor(vg, shine);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, viewW - lineFadeW, y + 1.0f, lineFadeW, 1.0f);
    nvgFillPaint(vg, nvgLinearGradient(vg, viewW - lineFadeW, y, viewW, y, shine, nvgRGBA(255, 255, 255, 0)));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, y + h - 1.0f, lineFadeW, 1.0f);
    nvgFillPaint(vg, nvgLinearGradient(vg, 0.0f, y, lineFadeW, y, nvgRGBA(0, 0, 0, 0), shadow));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, lineSolidX, y + h - 1.0f, lineSolidW, 1.0f);
    nvgFillColor(vg, shadow);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRect(vg, viewW - lineFadeW, y + h - 1.0f, lineFadeW, 1.0f);
    nvgFillPaint(vg, nvgLinearGradient(vg, viewW - lineFadeW, y, viewW, y, shadow, nvgRGBA(0, 0, 0, 0)));
    nvgFill(vg);
}

const char* repeatModeLabel(int mode) {
    if (mode == PlayerRepeatAll) return t("player.repeat_all");
    if (mode == PlayerRepeatOne) return t("player.repeat_one");
    return t("player.repeat_off_short");
}

void drawSettingsPanel(NVGcontext* vg, int font, const PlayerState& player, float viewW) {
    const int rotation = player.rotationDegrees % 360;
    const float viewH = (rotation == 90 || rotation == 270) ? kWidth : kHeight;
    const float margin = viewW < 640.0f ? 24.0f : 40.0f;
    const float contentX = margin + 42.0f;
    const float contentRight = viewW - margin - 36.0f;
    const float rowW = viewW;
    const float rowTop = 116.0f;
    const float rowPitch = 66.0f;
    const float rowH = 58.0f;
    const float sliderX = viewW * 0.42f;
    const float sliderW = viewW * 0.52f - margin;

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, viewW, viewH);
    nvgFillColor(vg, nvgRGBA(48, 48, 52, 191));
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFontSize(vg, 25.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, margin, 42.0f, t("player.settings.title"), nullptr);
    if (player.fileName[0]) {
        drawMarqueeText(vg, font, margin, 68.0f, viewW - margin * 2.0f, 15.0f,
                        ui::textSecondary(), player.fileName, true);
    }

    for (int i = 0; i < 3; ++i) {
        const float y = rowTop + rowPitch * i;
        drawSettingsBand(vg, y, viewW, rowH);
    }

    const float loopY = rowTop;
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, contentX, loopY + 23.0f, t("player.loop_playback"), nullptr);
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, contentX, loopY + 44.0f, t("player.loop"), nullptr);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, player.repeatMode != PlayerRepeatOff ? ui::accent() : ui::textMuted());
    nvgFontSize(vg, 20.0f);
    nvgText(vg, contentRight, loopY + 29.0f, repeatModeLabel(player.repeatMode), nullptr);

    const float speedY = rowTop + rowPitch;
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, contentX, speedY + 29.0f, t("player.speed_hint"), nullptr);
    drawSpeedSlider(vg, font, sliderX, speedY + 7.0f, sliderW, player.speed > 0.0 ? player.speed : 1.0);

    const float autoRotateY = rowTop + rowPitch * 2.0f;
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, contentX, autoRotateY + 23.0f, t("player.auto_rotate_hint"), nullptr);
    nvgFontSize(vg, 22.0f);
    nvgFillColor(vg, ui::textPrimary());
    nvgText(vg, contentX, autoRotateY + 44.0f, t("player.auto_rotate"), nullptr);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, player.autoRotateEnabled ? ui::accent() : ui::textMuted());
    nvgText(vg, contentRight, autoRotateY + 29.0f, player.autoRotateEnabled ? "ON" : "OFF", nullptr);

    ui::drawKeyCapsule(vg, font, margin, viewH - 32.0f, "SELECT", false);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 14.0f);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, margin + 78.0f, viewH - 21.0f, t("player.loop"), nullptr);
    ui::drawKeyCapsule(vg, font, margin + 162.0f, viewH - 32.0f, "L/R", false);
    nvgText(vg, margin + 218.0f, viewH - 21.0f, t("player.speed_hint"), nullptr);
    ui::drawKeyCapsule(vg, font, margin + 292.0f, viewH - 32.0f, "△", false);
    nvgText(vg, margin + 338.0f, viewH - 21.0f, t("player.auto_rotate"), nullptr);
    ui::drawKeyCapsule(vg, font, viewW - margin - 84.0f, viewH - 32.0f, "×", false);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, viewW - margin, viewH - 21.0f, t("hint.back"), nullptr);
}

void formatPlayerMeta(const PlayerState& player, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (player.videoWidth > 0 && player.videoHeight > 0) {
        std::snprintf(out, outSize, "%dx%d", player.videoWidth, player.videoHeight);
    }
    if (player.videoCodec[0] || player.audioCodec[0]) {
        char codecs[144];
        if (player.videoCodec[0] && player.audioCodec[0]) {
            std::snprintf(codecs, sizeof(codecs), "%s / %s", player.videoCodec, player.audioCodec);
        } else {
            std::snprintf(codecs, sizeof(codecs), "%s%s", player.videoCodec, player.audioCodec);
        }
        const size_t used = std::strlen(out);
        std::snprintf(out + used, used < outSize ? outSize - used : 0,
                      "%s%s", used > 0 ? "   " : "", codecs);
    }
    if (!out[0] && player.message[0]) {
        std::snprintf(out, outSize, "%s", player.message);
    }
    if (player.speed > 0.0) {
        const size_t used = std::strlen(out);
        std::snprintf(out + used, used < outSize ? outSize - used : 0,
                      "%s%.2gx", used > 0 ? "   " : "", player.speed);
    }
}

} // namespace

void drawPlayerOverlay(NVGcontext* vg, int font, const PlayerState& player, float viewW, float viewH) {
    if (!player.hudVisible && player.swipeSeeking) {
        drawSwipeSeekOverlay(vg, font, player, viewW, viewH);
        return;
    }

    const float anim = clampFloat(player.hudAnim, 0.0f, 1.0f);
    if (anim <= 0.01f && !player.swipeSeeking) return;
    const float eased = anim * anim * (3.0f - 2.0f * anim);
    const NVGcolor white = ui::textPrimary();
    const PlayerHudLayout hud = buildPlayerHudLayout(vg, font, player, viewW, viewH);

    if (player.settingsVisible) {
        nvgSave(vg);
        nvgGlobalAlpha(vg, eased);
        drawSettingsPanel(vg, font, player, viewW);
        nvgRestore(vg);
        if (player.swipeSeeking) {
            drawSwipeSeekOverlay(vg, font, player, viewW, viewH);
        }
        return;
    }

    nvgSave(vg);
    nvgGlobalAlpha(vg, eased);
    nvgTranslate(vg, 0.0f, -74.0f * (1.0f - eased));

    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, 88.0f,
                                           nvgRGBA(2, 12, 30, 220), nvgRGBA(2, 12, 30, 0));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, viewW, 88.0f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, 20.0f, 28.0f, "×", nullptr);

    drawMarqueeText(vg, font, 54.0f, 34.0f, viewW - 250.0f, 19.0f,
                    white, player.fileName[0] ? player.fileName : t("app.title"), true);

    char meta[192];
    formatPlayerMeta(player, meta, sizeof(meta));
    if (meta[0]) {
        drawMarqueeText(vg, font, 54.0f, 60.0f, viewW - 250.0f, 13.0f, ui::textSecondary(), meta, true);
    }

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textSecondary());
    for (int i = 0; i < hud.topCount; ++i) {
        if (hud.top[i].action == PlayerHudActionBack) continue;
        if (hud.top[i].action == PlayerHudActionSettings) {
            drawMenuDots(vg, hud.top[i]);
        } else if (hud.top[i].action == PlayerHudActionSpeed) {
            drawTopPlainLabel(vg, font, hud.top[i]);
        } else {
            drawHudButton(vg, font, hud.top[i]);
        }
    }
    nvgRestore(vg);

    nvgSave(vg);
    nvgGlobalAlpha(vg, eased);
    nvgTranslate(vg, 0.0f, 82.0f * (1.0f - eased));

    const NVGpaint bottom = nvgLinearGradient(vg, 0.0f, viewH - 126.0f, 0.0f, viewH,
                                              nvgRGBA(2, 12, 30, 0), nvgRGBA(2, 12, 30, 226));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, viewH - 126.0f, viewW, 126.0f);
    nvgFillPaint(vg, bottom);
    nvgFill(vg);

    double displayPosition = gTouchDraggingBar ? gTouchScrubTarget : player.positionSeconds;
    if (displayPosition < 0.0) displayPosition = 0.0;
    if (player.durationSeconds > 0.0 && displayPosition > player.durationSeconds) {
        displayPosition = player.durationSeconds;
    }

    char pos[16], dur[16];
    formatTime(displayPosition, pos, sizeof(pos));
    formatTime(player.durationSeconds, dur, sizeof(dur));

    const float barY = viewH - 94.0f;
    const float timeY = barY - 22.0f;
    const float barX = 46.0f;
    const float barW = viewW - 92.0f;
    const bool expandedBar = gTouchDraggingBar;
    const float trackH = expandedBar ? 10.0f : 5.0f;
    const float thumbR = trackH * 0.5f;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 16.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, barX, timeY, pos, nullptr);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, ui::textSecondary());
    nvgText(vg, barX + barW, timeY, dur, nullptr);

    float frac = 0.0f;
    if (player.durationSeconds > 0.0) {
        frac = static_cast<float>(displayPosition / player.durationSeconds);
        frac = clampFloat(frac, 0.0f, 1.0f);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - trackH * 0.5f, barW, trackH, trackH * 0.5f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, expandedBar ? 92 : 64));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - trackH * 0.5f, barW * frac, trackH, trackH * 0.5f);
    nvgFillColor(vg, ui::accent());
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, barX + barW * frac, barY, thumbR);
    nvgFillColor(vg, ui::accent());
    nvgFill(vg);
    if (expandedBar) {
        nvgStrokeWidth(vg, 2.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 180));
        nvgStroke(vg);
    }

    for (int i = 0; i < hud.bottomCount; ++i) {
        drawBottomIconButton(vg, hud.bottom[i], player);
    }
    nvgRestore(vg);

    if (player.swipeSeeking) {
        drawSwipeSeekOverlay(vg, font, player, viewW, viewH);
    }
}
