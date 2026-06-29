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

void addTopButton(PlayerHudLayout* layout, PlayerHudAction action, float x, float y, float w, float h,
                  const char* label, bool active) {
    if (!layout || layout->topCount >= static_cast<int>(sizeof(layout->top) / sizeof(layout->top[0]))) return;
    setButton(&layout->top[layout->topCount++], action, x, y, w, h, "", label, active, true);
}

bool contains(const PlayerHudButton& button, float x, float y) {
    return x >= button.x && x <= button.x + button.w && y >= button.y && y <= button.y + button.h;
}

bool containsCircleButton(const PlayerHudButton& button, float x, float y) {
    const float cx = button.x + button.w * 0.5f;
    const float cy = button.y + button.h * 0.5f;
    const float r = button.w < button.h ? button.w * 0.5f : button.h * 0.5f;
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

void addBottomIcon(PlayerHudLayout* layout, PlayerHudAction action, float cx, float cy,
                   const char* label, bool active) {
    if (!layout || layout->bottomCount >= static_cast<int>(sizeof(layout->bottom) / sizeof(layout->bottom[0]))) return;
    setButton(&layout->bottom[layout->bottomCount++], action, cx - 22.0f, cy - 22.0f, 44.0f, 44.0f,
              "", label, active, true);
}

} // namespace

PlayerHudLayout buildPlayerHudLayout(NVGcontext* vg, int font, const PlayerState& player,
                                     float viewW, float viewH) {
    (void)vg;
    (void)font;
    PlayerHudLayout layout = {};

    addTopButton(&layout, PlayerHudActionBack, 0.0f, 0.0f, 72.0f, 58.0f, "", false);
    char speedLine[32];
    std::snprintf(speedLine, sizeof(speedLine), "%.2gx", player.speed > 0.0 ? player.speed : 1.0);
    addTopButton(&layout, PlayerHudActionSpeed, viewW - 164.0f, 0.0f, 104.0f, 58.0f,
                 speedLine, player.speedSliderVisible != 0);
    addTopButton(&layout, PlayerHudActionSettings, viewW - 58.0f, 0.0f, 58.0f, 58.0f,
                 "", player.settingsVisible != 0);

    const float controlsY = viewH - 50.0f;
    const float controlsX = viewW * 0.5f;
    addBottomIcon(&layout, PlayerHudActionShuffle, controlsX - 96.0f, controlsY, "", player.shufflePlayback != 0);
    addBottomIcon(&layout, PlayerHudActionPrevious, controlsX - 48.0f, controlsY, "", false);
    addBottomIcon(&layout, PlayerHudActionPlayPause, controlsX, controlsY, "", false);
    addBottomIcon(&layout, PlayerHudActionNext, controlsX + 48.0f, controlsY, "", false);
    addBottomIcon(&layout, PlayerHudActionLoop, controlsX + 96.0f, controlsY, "", player.repeatMode != PlayerRepeatOff);
    return layout;
}

PlayerHudAction hitPlayerHudButton(const PlayerHudLayout& layout, float x, float y) {
    if (layout.hasCenter && layout.center.clickable && containsCircleButton(layout.center, x, y)) {
        return layout.center.action;
    }
    for (int i = 0; i < layout.topCount; ++i) {
        if (layout.top[i].clickable && contains(layout.top[i], x, y)) return layout.top[i].action;
    }
    for (int i = 0; i < layout.bottomCount; ++i) {
        if (layout.bottom[i].clickable && contains(layout.bottom[i], x, y)) return layout.bottom[i].action;
    }
    return PlayerHudActionNone;
}
