#pragma once

#include "Core/Types.hpp"

struct NVGcontext;

enum PlayerHudAction {
    PlayerHudActionNone = 0,
    PlayerHudActionBack,
    PlayerHudActionPlayPause,
    PlayerHudActionHud,
    PlayerHudActionSeekBack,
    PlayerHudActionSeekForward,
    PlayerHudActionLoop,
    PlayerHudActionSettings,
    PlayerHudActionAutoRotate,
    PlayerHudActionSpeed,
};

struct PlayerHudButton {
    PlayerHudAction action;
    float x;
    float y;
    float w;
    float h;
    char key[24];
    char label[80];
    bool active;
    bool clickable;
};

struct PlayerHudLayout {
    PlayerHudButton top[3];
    int topCount;
    PlayerHudButton bottom[8];
    int bottomCount;
};

PlayerHudLayout buildPlayerHudLayout(NVGcontext* vg, int font, const PlayerState& player,
                                     float viewW, float viewH);
PlayerHudAction hitPlayerHudButton(const PlayerHudLayout& layout, float x, float y);
