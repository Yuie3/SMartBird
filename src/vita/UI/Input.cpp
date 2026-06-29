#include "UI/Input.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Player/PlayerHudLayout.hpp"
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
    constexpr float rowX = 80.0f;
    constexpr float rowW = 800.0f;
    const float rowY[] = {120.0f, 156.0f, 192.0f, 228.0f, 264.0f, 300.0f, 336.0f};
    for (int i = 0; i <= ConnectLocalPath; ++i) {
        if (hitRect(x, y, rowX, rowY[i] - 24.0f, rowW, 34.0f)) return i;
    }
    if (hitRect(x, y, 58.0f, 386.0f, 268.0f, 68.0f)) return ConnectSmbAction;
    if (hitRect(x, y, 346.0f, 386.0f, 268.0f, 68.0f)) return ConnectLocalAction;
    if (hitRect(x, y, 634.0f, 386.0f, 268.0f, 68.0f)) return ConnectHiddenAction;
    return -1;
}

int listIndexAtPoint(float x, float y, int listTop, int count) {
    if (x < 0.0f || x > kWidth || y < kListTopY) return -1;
    const int row = static_cast<int>((y - kListTopY) / kListRowPitch);
    if (row < 0 || row >= kVisibleEntries) return -1;
    const float rowTop = kListTopY + row * kListRowPitch;
    if (!hitRect(x, y, 0.0f, rowTop, kWidth, kListRowHeight)) return -1;
    const int index = listTop + row;
    return index >= 0 && index < count ? index : -1;
}

double speedFromSliderFrac(float frac) {
    const float f = clampFloat(frac, 0.0f, 1.0f);
    if (f <= 0.5f) return 0.5 + static_cast<double>(f / 0.5f) * 0.5;
    return 1.0 + static_cast<double>((f - 0.5f) / 0.5f);
}

bool handlePlayerTouch(bool touching, float x, float y, NVGcontext* vg, int font) {
    PlayerState player = {};
    lockScan();
    player = gPlayer;
    unlockScan();

    const int rotation = player.rotationDegrees % 360;
    const bool rotated = rotation == 90 || rotation == 270;
    const float viewW = rotated ? kHeight : kWidth;
    const float viewH = rotated ? kWidth : kHeight;
    if (rotation == 90) {
        const float mappedX = y;
        const float mappedY = kWidth - x;
        x = mappedX;
        y = mappedY;
    } else if (rotation == 180) {
        x = kWidth - x;
        y = kHeight - y;
    } else if (rotation == 270) {
        const float mappedX = kHeight - y;
        const float mappedY = x;
        x = mappedX;
        y = mappedY;
    }

    const float barX = 46.0f;
    const float barY = viewH - 94.0f;
    const float barW = viewW - 92.0f;
    const float settingsX = 0.0f;
    const float settingsY = 0.0f;
    const float settingsW = viewW;
    const float settingsH = viewH;
    const float settingsMargin = viewW < 640.0f ? 24.0f : 40.0f;
    const float settingsRowX = 0.0f;
    const float settingsRowW = viewW;
    const float settingsRowTop = 116.0f;
    const float settingsRowPitch = 66.0f;
    const float settingsRowH = 58.0f;
    const float popupSpeedSliderX = viewW - 284.0f;
    const float popupSpeedSliderY = 50.0f;
    const float popupSpeedSliderW = 238.0f;
    const float settingsSpeedSliderX = viewW * 0.42f;
    const float settingsSpeedSliderY = settingsRowTop + settingsRowPitch + 7.0f;
    const float settingsSpeedSliderW = viewW * 0.52f - settingsMargin;
    const float activeSpeedSliderX = player.settingsVisible ? settingsSpeedSliderX : popupSpeedSliderX;
    const float activeSpeedSliderY = player.settingsVisible ? settingsSpeedSliderY : popupSpeedSliderY;
    const float activeSpeedSliderW = player.settingsVisible ? settingsSpeedSliderW : popupSpeedSliderW;
    const PlayerHudLayout hud = buildPlayerHudLayout(vg, font, player, viewW, viewH);

    const bool overlayShown = player.hudVisible &&
        (player.loading || player.paused || player.overlayFrames > 0 ||
         player.speedSliderVisible || player.settingsVisible || !player.hasFrame);
    const bool hitProgressBar = overlayShown && !player.settingsVisible &&
        y >= barY - 30.0f && y <= barY + 30.0f &&
        x >= barX - 22.0f && x <= barX + barW + 22.0f;
    const bool hitPopupSpeedSlider = overlayShown && player.speedSliderVisible &&
        hitRect(x, y, popupSpeedSliderX, popupSpeedSliderY, popupSpeedSliderW, 44.0f);
    const bool hitSettingsSpeedSlider = overlayShown && player.settingsVisible &&
        hitRect(x, y, settingsSpeedSliderX, settingsSpeedSliderY, settingsSpeedSliderW, 44.0f);
    const bool hitSpeedSlider = hitPopupSpeedSlider || hitSettingsSpeedSlider;
    const bool hitSettingsPanel = overlayShown && player.settingsVisible &&
        hitRect(x, y, settingsX, settingsY, settingsW, settingsH);
    const PlayerHudAction touchedAction = (overlayShown && !player.settingsVisible)
        ? hitPlayerHudButton(hud, x, y)
        : PlayerHudActionNone;
    const bool hitHudButton = touchedAction != PlayerHudActionNone;

    if (touching && !gTouchPrev) {
        gTouchStartX = x;
        gTouchStartY = y;
        gTouchLastX = x;
        gTouchLastY = y;
        gTouchDraggingSpeed = false;
        gTouchSwipeSeeking = false;
        gTouchHudAction = static_cast<int>(touchedAction);
        gTouchStartedOnPlayerControl = hitProgressBar || hitSpeedSlider || hitHudButton || hitSettingsPanel;
        gTouchSwipeSeekStart = player.positionSeconds;
        gTouchSwipeSeekTarget = player.positionSeconds;
        lockScan();
        gPlayer.swipeSeeking = 0;
        gPlayer.swipeSeekStartSeconds = player.positionSeconds;
        gPlayer.swipeSeekOffsetSeconds = 0.0;
        gPlayer.swipeSeekTargetSeconds = player.positionSeconds;
        unlockScan();
        if (hitProgressBar) {
            gTouchDraggingBar = true;
        } else if (hitSpeedSlider) {
            gTouchDraggingSpeed = true;
        }
    }

    if (touching && !gTouchDraggingBar && !gTouchDraggingSpeed && !gTouchSwipeSeeking &&
        !gTouchStartedOnPlayerControl && player.durationSeconds > 0.0) {
        const float dx = x - gTouchStartX;
        const float dy = y - gTouchStartY;
        const float absDx = dx > 0.0f ? dx : -dx;
        const float absDy = dy > 0.0f ? dy : -dy;
        if (absDx >= 20.0f && absDx > absDy) {
            gTouchSwipeSeeking = true;
        }
    }

    if (touching && gTouchDraggingBar) {
        const float frac = clampFloat((x - barX) / barW, 0.0f, 1.0f);
        gTouchScrubTarget = frac * (player.durationSeconds > 0.0 ? player.durationSeconds : 0.0);
        showPlayerOverlay();
    }

    if (touching && gTouchDraggingSpeed) {
        const float frac = clampFloat((x - (activeSpeedSliderX + 18.0f)) / (activeSpeedSliderW - 36.0f), 0.0f, 1.0f);
        setMpvSpeed(speedFromSliderFrac(frac));
        showPlayerOverlay();
    }

    if (touching && gTouchSwipeSeeking) {
        constexpr double baseSensitivity = 0.1;
        const double offset = static_cast<double>(x - gTouchStartX) * baseSensitivity;
        double target = gTouchSwipeSeekStart + offset;
        if (target < 0.0) target = 0.0;
        if (player.durationSeconds > 0.0 && target > player.durationSeconds) target = player.durationSeconds;
        gTouchSwipeSeekTarget = target;
        lockScan();
        gPlayer.swipeSeeking = 1;
        gPlayer.swipeSeekStartSeconds = gTouchSwipeSeekStart;
        gPlayer.swipeSeekOffsetSeconds = target - gTouchSwipeSeekStart;
        gPlayer.swipeSeekTargetSeconds = target;
        unlockScan();
    }

    bool requestBack = false;
    if (!touching && gTouchPrev) {
        const float dx = gTouchLastX - gTouchStartX;
        const float dy = gTouchLastY - gTouchStartY;
        const bool isTap = dx * dx + dy * dy < 24.0f * 24.0f;
        const float ux = gTouchLastX;
        const float uy = gTouchLastY;
        const PlayerHudAction releasedAction = (overlayShown && !player.settingsVisible)
            ? hitPlayerHudButton(hud, ux, uy)
            : PlayerHudActionNone;

        if (gTouchSwipeSeeking) {
            seekMpvAbsolute(gTouchSwipeSeekTarget);
            showPlayerOverlay();
        } else if (gTouchDraggingSpeed) {
            const float frac = clampFloat((ux - (activeSpeedSliderX + 18.0f)) / (activeSpeedSliderW - 36.0f), 0.0f, 1.0f);
            setMpvSpeed(speedFromSliderFrac(frac));
            showPlayerOverlay();
        } else if (gTouchDraggingBar) {
            seekMpvAbsolute(gTouchScrubTarget);
            showPlayerOverlay();
        } else if (isTap) {
            if (!overlayShown) {
                showPlayerOverlay();
            } else if (releasedAction == PlayerHudActionBack) {
                requestBack = true;
            } else if ((player.speedSliderVisible || player.settingsVisible) &&
                       hitRect(ux, uy, activeSpeedSliderX, activeSpeedSliderY, activeSpeedSliderW, 44.0f)) {
                const float frac = clampFloat((ux - (activeSpeedSliderX + 18.0f)) / (activeSpeedSliderW - 36.0f), 0.0f, 1.0f);
                setMpvSpeed(speedFromSliderFrac(frac));
                showPlayerOverlay();
            } else if (player.settingsVisible &&
                       hitRect(ux, uy, settingsRowX, settingsRowTop, settingsRowW, settingsRowH)) {
                toggleMpvLoop();
            } else if (player.settingsVisible &&
                       hitRect(ux, uy, settingsRowX, settingsRowTop + settingsRowPitch * 2.0f,
                               settingsRowW, settingsRowH)) {
                toggleMpvAutoRotate();
            } else if (player.settingsVisible &&
                       hitRect(ux, uy, settingsX, settingsY, settingsW, settingsH)) {
                showPlayerOverlay();
            } else if (uy >= barY - 30.0f && uy <= barY + 30.0f &&
                       ux >= barX - 22.0f && ux <= barX + barW + 22.0f) {
                const float frac = clampFloat((ux - barX) / barW, 0.0f, 1.0f);
                seekMpvAbsolute(frac * (player.durationSeconds > 0.0 ? player.durationSeconds : 0.0));
                showPlayerOverlay();
            } else if (releasedAction != PlayerHudActionNone) {
                switch (releasedAction) {
                case PlayerHudActionPlayPause:
                    if (!player.waitingForValidation) {
                        showPlayerOverlay();
                        setMpvPause(!player.paused);
                    }
                    break;
                case PlayerHudActionHud:
                    togglePlayerOverlay();
                    break;
                case PlayerHudActionSeekBack:
                    showPlayerOverlay();
                    seekMpvRelative(-10.0);
                    break;
                case PlayerHudActionSeekForward:
                    showPlayerOverlay();
                    seekMpvRelative(10.0);
                    break;
                case PlayerHudActionShuffle:
                    toggleMpvShuffle();
                    break;
                case PlayerHudActionPrevious:
                    lockScan();
                    gPlayerPendingAction = PlayerPendingPrevious;
                    unlockScan();
                    showPlayerOverlay();
                    break;
                case PlayerHudActionNext:
                    lockScan();
                    gPlayerPendingAction = PlayerPendingNext;
                    unlockScan();
                    showPlayerOverlay();
                    break;
                case PlayerHudActionLoop:
                    toggleMpvLoop();
                    break;
                case PlayerHudActionSettings:
                    togglePlayerSettingsPanel();
                    break;
                case PlayerHudActionAutoRotate:
                    toggleMpvAutoRotate();
                    break;
                case PlayerHudActionSpeed:
                    cycleMpvSpeedPreset();
                    break;
                default:
                    break;
                }
            } else {
                hidePlayerOverlay();
            }
        }
        gTouchDraggingBar = false;
        gTouchDraggingSpeed = false;
        gTouchSwipeSeeking = false;
        gTouchStartedOnPlayerControl = false;
        gTouchHudAction = 0;
        lockScan();
        gPlayer.swipeSeeking = 0;
        gPlayer.swipeSeekOffsetSeconds = 0.0;
        gPlayer.swipeSeekTargetSeconds = gPlayer.positionSeconds;
        unlockScan();
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
