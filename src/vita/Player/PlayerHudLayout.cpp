#include "Player/PlayerHudLayout.hpp"

#include "Config/I18n.hpp"
#include "nanovg.h"

#include <cstdio>

namespace {

void setButton(PlayerHudButton* button, PlayerHudAction action, float x, float y, float w, float h,
               const char* key, const char* label, bool active, bool clickable) {
    if (!button) return;
    button->action = action;
    button->x = x;
    button->y = y;
    button->w = w;
    button->h = h;
    std::snprintf(button->key, sizeof(button->key), "%s", key ? key : "");
    std::snprintf(button->label, sizeof(button->label), "%s", label ? label : "");
    button->active = active;
    button->clickable = clickable;
}

float measureButtonWidth(NVGcontext* vg, int font, const char* key, const char* label) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    float keyBounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, key ? key : "", nullptr, keyBounds);
    nvgFontSize(vg, 14.0f);
    float labelBounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, label ? label : "", nullptr, labelBounds);
    return (keyBounds[2] - keyBounds[0]) + (labelBounds[2] - labelBounds[0]) + 34.0f;
}

void addTopButton(PlayerHudLayout* layout, PlayerHudAction action, float x, float y, float w,
                  const char* label, bool active) {
    if (!layout || layout->topCount >= static_cast<int>(sizeof(layout->top) / sizeof(layout->top[0]))) return;
    setButton(&layout->top[layout->topCount++], action, x, y, w, 28.0f, "", label, active, true);
}

void addBottomButton(NVGcontext* vg, int font, PlayerHudLayout* layout, PlayerHudAction action,
                     float* x, float y, float maxRight, const char* key, const char* label,
                     bool active = false, bool clickable = true) {
    if (!layout || !x || layout->bottomCount >= static_cast<int>(sizeof(layout->bottom) / sizeof(layout->bottom[0]))) return;
    const float w = measureButtonWidth(vg, font, key, label);
    if (*x + w > maxRight) return;
    setButton(&layout->bottom[layout->bottomCount++], action, *x, y - 14.0f, w, 28.0f,
              key, label, active, clickable);
    *x += w + 10.0f;
}

bool contains(const PlayerHudButton& button, float x, float y) {
    return x >= button.x && x <= button.x + button.w && y >= button.y && y <= button.y + button.h;
}

} // namespace

PlayerHudLayout buildPlayerHudLayout(NVGcontext* vg, int font, const PlayerState& player,
                                     float viewW, float viewH) {
    PlayerHudLayout layout = {};

    addTopButton(&layout, PlayerHudActionBack, 0.0f, 0.0f, 92.0f, "", false);
    addTopButton(&layout, PlayerHudActionLoop, viewW - 250.0f, 16.0f, 86.0f,
                 t("player.loop"), player.loopPlayback != 0);
    char speedLine[32];
    std::snprintf(speedLine, sizeof(speedLine), "%.2gx", player.speed > 0.0 ? player.speed : 1.0);
    addTopButton(&layout, PlayerHudActionSpeed, viewW - 150.0f, 16.0f, 104.0f,
                 speedLine, player.speedSliderVisible != 0);

    const float barX = 46.0f;
    const float rowY = viewH - 34.0f;
    const float maxRight = viewW - 132.0f;

    float x = barX;
    addBottomButton(vg, font, &layout, PlayerHudActionPlayPause, &x, rowY, maxRight, "○", t("hint.play"));
    addBottomButton(vg, font, &layout, PlayerHudActionSettings, &x, rowY, maxRight, "START", t("player.settings"), player.settingsVisible != 0);
    addBottomButton(vg, font, &layout, PlayerHudActionAutoRotate, &x, rowY, maxRight,
                    "△", t("player.auto_rotate"), player.autoRotateEnabled != 0);
    return layout;
}

PlayerHudAction hitPlayerHudButton(const PlayerHudLayout& layout, float x, float y) {
    for (int i = 0; i < layout.topCount; ++i) {
        if (layout.top[i].clickable && contains(layout.top[i], x, y)) return layout.top[i].action;
    }
    for (int i = 0; i < layout.bottomCount; ++i) {
        if (layout.bottom[i].clickable && contains(layout.bottom[i], x, y)) return layout.bottom[i].action;
    }
    return PlayerHudActionNone;
}
