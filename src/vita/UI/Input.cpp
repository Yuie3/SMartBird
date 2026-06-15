#include "UI/Input.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Player/VideoPlayer.hpp"
#include "Utils/Math.hpp"

bool hitCircle(float x, float y, float cx, float cy, float r) {
    const float dx = x - cx;
    const float dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

bool hitRect(float x, float y, float rx, float ry, float rw, float rh) {
    return x >= rx && x <= rx + rw && y >= ry && y <= ry + rh;
}

int connectFieldAtPoint(float x, float y) {
    constexpr float rowX = 58.0f;
    constexpr float rowW = 844.0f;
    const float rowY[] = {158.0f, 194.0f, 230.0f, 266.0f, 302.0f, 338.0f, 374.0f};
    for (int i = 0; i <= ConnectLocalPath; ++i) {
        if (hitRect(x, y, rowX, rowY[i] - 25.0f, rowW, 36.0f)) return i;
    }
    if (hitRect(x, y, 58.0f, 408.0f, 268.0f, 68.0f)) return ConnectSmbAction;
    if (hitRect(x, y, 346.0f, 408.0f, 268.0f, 68.0f)) return ConnectLocalAction;
    if (hitRect(x, y, 634.0f, 408.0f, 268.0f, 68.0f)) return ConnectHiddenAction;
    return -1;
}

int listIndexAtPoint(float x, float y, int listTop, int count) {
    if (x < 40.0f || x > 920.0f || y < 178.0f) return -1;
    const int row = static_cast<int>((y - 178.0f) / 38.0f);
    if (row < 0 || row >= kVisibleEntries) return -1;
    const float rowTop = 178.0f + row * 38.0f;
    if (!hitRect(x, y, 40.0f, rowTop, 880.0f, 36.0f)) return -1;
    const int index = listTop + row;
    return index >= 0 && index < count ? index : -1;
}

bool handlePlayerTouch(bool touching, float x, float y) {
    PlayerState player = {};
    lockScan();
    player = gPlayer;
    unlockScan();

    const bool rotated = player.rotationDegrees == 270;
    const float viewW = rotated ? kHeight : kWidth;
    const float viewH = rotated ? kWidth : kHeight;
    if (rotated) {
        const float mappedX = kHeight - y;
        const float mappedY = x;
        x = mappedX;
        y = mappedY;
    }

    const float transportY = viewH - 82.0f;
    const float playX = viewW - 44.0f;
    const float forwardX = viewW - 118.0f;
    const float backX = viewW - 192.0f;
    const float barX = 250.0f;
    const float barY = viewH - 24.0f;
    const float barW = viewW - barX - 42.0f;

    const bool overlayShown = player.loading || player.paused || player.overlayFrames > 0 || !player.hasFrame;

    if (touching && !gTouchPrev) {
        gTouchStartX = x;
        gTouchStartY = y;
        gTouchLastX = x;
        gTouchLastY = y;
        if (overlayShown && y >= barY - 18.0f && y <= barY + 18.0f &&
            x >= barX - 16.0f && x <= barX + barW + 16.0f) {
            gTouchDraggingBar = true;
        }
    }

    if (touching && gTouchDraggingBar) {
        const float frac = clampFloat((x - barX) / barW, 0.0f, 1.0f);
        gTouchScrubTarget = frac * (player.durationSeconds > 0.0 ? player.durationSeconds : 0.0);
        showPlayerOverlay();
    }

    bool requestBack = false;
    if (!touching && gTouchPrev) {
        const float dx = gTouchLastX - gTouchStartX;
        const float dy = gTouchLastY - gTouchStartY;
        const bool isTap = dx * dx + dy * dy < 24.0f * 24.0f;
        const float ux = gTouchLastX;
        const float uy = gTouchLastY;

        if (gTouchDraggingBar) {
            seekMpvAbsolute(gTouchScrubTarget);
            showPlayerOverlay();
        } else if (isTap) {
            if (!overlayShown) {
                showPlayerOverlay();
            } else if (uy < 46.0f && (ux < 80.0f || ux > viewW - 110.0f)) {
                requestBack = true;
            } else if (hitCircle(ux, uy, playX, transportY, 32.0f) && !player.waitingForValidation) {
                showPlayerOverlay();
                setMpvPause(!player.paused);
            } else if (hitCircle(ux, uy, backX, transportY, 30.0f)) {
                showPlayerOverlay();
                seekMpvRelative(-10.0);
            } else if (hitCircle(ux, uy, forwardX, transportY, 30.0f)) {
                showPlayerOverlay();
                seekMpvRelative(10.0);
            } else if (uy >= barY - 18.0f && uy <= barY + 18.0f &&
                       ux >= barX - 16.0f && ux <= barX + barW + 16.0f) {
                const float frac = clampFloat((ux - barX) / barW, 0.0f, 1.0f);
                seekMpvAbsolute(frac * (player.durationSeconds > 0.0 ? player.durationSeconds : 0.0));
                showPlayerOverlay();
            } else {
                hidePlayerOverlay();
            }
        }
        gTouchDraggingBar = false;
    }

    if (touching) {
        gTouchLastX = x;
        gTouchLastY = y;
    }
    gTouchPrev = touching;
    return requestBack;
}

bool pressed(unsigned int current, unsigned int previous, unsigned int button) {
    return (current & button) && !(previous & button);
}

bool repeatButton(unsigned int current, unsigned int previous, unsigned int button, int* holdFrames) {
    if (!(current & button)) {
        *holdFrames = 0;
        return false;
    }
    if (!(previous & button)) {
        *holdFrames = 1;
        return true;
    }
    ++(*holdFrames);
    return *holdFrames >= 18 && ((*holdFrames - 18) % 5) == 0;
}

float analogAxis(unsigned char value) {
    const int centered = static_cast<int>(value) - 128;
    if (centered > -22 && centered < 22) return 0.0f;
    return static_cast<float>(centered) / 127.0f;
}
