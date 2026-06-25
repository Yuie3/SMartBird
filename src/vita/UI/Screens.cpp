#include "UI/Screens.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/I18n.hpp"
#include "Config/Settings.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Image/ImageViewer.hpp"
#include "Player/PlayerOverlay.hpp"
#include "Player/VideoPlayer.hpp"
#include "UI/ImeInput.hpp"
#include "UI/Theme.hpp"
#include "UI/Widgets.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Text.hpp"
#include "nanovg.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

void maskPassword(const char* src, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    size_t len = src ? std::strlen(src) : 0;
    if (len >= outSize) len = outSize - 1;
    for (size_t i = 0; i < len; ++i) out[i] = '*';
    out[len] = '\0';
}

void drawAppChrome(NVGcontext* vg) {
    ui::drawAppBackground(vg);
}

struct ListMotion {
    bool initialized;
    AppMode mode;
    int count;
    float visualTop;
    float visualSelected;
};

struct FocusRectMotion {
    bool initialized;
    AppMode mode;
    float x;
    float y;
    float w;
    float h;
};

ListMotion gBrowserMotion = {};
ListMotion gHiddenMotion = {};
FocusRectMotion gConnectMotion = {};

float pulseAmount(int frames) {
    if (frames <= 0) return 0.0f;
    if (frames > 8) frames = 8;
    return static_cast<float>(frames) / 8.0f;
}

float approachFloat(float current, float target, float factor) {
    const float diff = target - current;
    if (std::fabs(diff) < 0.01f) return target;
    return current + diff * factor;
}

void updateListMotion(ListMotion* motion, AppMode mode, int listTop, int selected, int count) {
    const float targetTop = static_cast<float>(listTop);
    const float targetSelected = static_cast<float>(selected);
    if (!motion->initialized || motion->mode != mode || motion->count != count ||
        std::fabs(motion->visualTop - targetTop) > static_cast<float>(kVisibleEntries) ||
        std::fabs(motion->visualSelected - targetSelected) > static_cast<float>(kVisibleEntries)) {
        motion->initialized = true;
        motion->mode = mode;
        motion->count = count;
        motion->visualTop = targetTop;
        motion->visualSelected = targetSelected;
        return;
    }
    motion->visualTop = approachFloat(motion->visualTop, targetTop, 0.34f);
    motion->visualSelected = approachFloat(motion->visualSelected, targetSelected, 0.42f);
}

void connectFocusRect(int focus, float* x, float* y, float* w, float* h) {
    if (focus <= ConnectLocalPath) {
        const float rowY[] = {120.0f, 156.0f, 192.0f, 228.0f, 264.0f, 300.0f, 336.0f};
        *x = 80.0f;
        *y = rowY[focus] - 24.0f;
        *w = 800.0f;
        *h = 34.0f;
        return;
    }
    const float buttonX[] = {58.0f, 346.0f, 634.0f};
    const int index = focus - ConnectSmbAction;
    *x = buttonX[index < 0 ? 0 : (index > 2 ? 2 : index)];
    *y = 386.0f;
    *w = 268.0f;
    *h = 68.0f;
}

void updateFocusRectMotion(FocusRectMotion* motion, AppMode mode, int focus,
                           float* x, float* y, float* w, float* h) {
    float tx = 0.0f, ty = 0.0f, tw = 0.0f, th = 0.0f;
    connectFocusRect(focus, &tx, &ty, &tw, &th);
    if (!motion->initialized || motion->mode != mode ||
        std::fabs(motion->y - ty) > 120.0f || std::fabs(motion->x - tx) > 360.0f) {
        motion->initialized = true;
        motion->mode = mode;
        motion->x = tx;
        motion->y = ty;
        motion->w = tw;
        motion->h = th;
    } else {
        motion->x = approachFloat(motion->x, tx, 0.40f);
        motion->y = approachFloat(motion->y, ty, 0.40f);
        motion->w = approachFloat(motion->w, tw, 0.40f);
        motion->h = approachFloat(motion->h, th, 0.40f);
    }
    *x = motion->x;
    *y = motion->y;
    *w = motion->w;
    *h = motion->h;
}

int clampIndex(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

} // namespace

void renderUi(NVGcontext* vg, int font, const RuntimeStatus& runtime, const ScanState& scan,
              const PlayerState& player, const ImageState& image,
              AppMode mode, int selected, int listTop, int connectFocus, int actionPulseFrames) {
    (void)runtime;
    const float pulse = pulseAmount(actionPulseFrames);

    if (mode == ModePlayer) {
        if (player.errorTitle[0]) {
            drawAppChrome(vg);
            ui::drawTopBar(vg, font, player.errorTitle, player.fileName);
            ui::drawGlassPanel(vg, 72.0f, 148.0f, 816.0f, 230.0f, ui::kPanelRadius, true);
            drawText(vg, font, 104.0f, 206.0f, 21.0f, ui::textPrimary(), player.errorReason);
            drawText(vg, font, 104.0f, 270.0f, 18.0f, ui::textSecondary(),
                     std::strcmp(player.errorTitle, t("error.resolution.title")) == 0
                         ? t("error.resolution.hint")
                         : t("error.convert.hint"));
            const FooterHint hints[] = {
                {"×", t("hint.back")},
            };
            drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
            return;
        }

        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);

        if (player.hasFrame && gPlayer.nvgImage) {
            drawVideo(vg, 0.0f, 0.0f, kWidth, kHeight);
        } else {
            nvgFontFaceId(vg, font);
            nvgFontSize(vg, 22.0f);
            nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgFillColor(vg, nvgRGB(150, 155, 175));
            nvgText(vg, kWidth * 0.5f, kHeight * 0.5f,
                    player.loading ? t("player.loading") : t("player.waiting"), nullptr);
        }

        if (player.swipeSeeking || player.hudAnim > 0.01f ||
            (player.hudVisible && (player.loading || player.paused || player.overlayFrames > 0 ||
                                   player.speedSliderVisible || player.settingsVisible || !player.hasFrame))) {
            const int rotation = player.rotationDegrees % 360;
            if (rotation == 90) {
                nvgSave(vg);
                nvgTranslate(vg, kWidth, 0.0f);
                nvgRotate(vg, 0.5f * kPi);
                drawPlayerOverlay(vg, font, player, kHeight, kWidth);
                nvgRestore(vg);
            } else if (rotation == 180) {
                nvgSave(vg);
                nvgTranslate(vg, kWidth, kHeight);
                nvgRotate(vg, kPi);
                drawPlayerOverlay(vg, font, player, kWidth, kHeight);
                nvgRestore(vg);
            } else if (rotation == 270) {
                nvgSave(vg);
                nvgTranslate(vg, 0.0f, kHeight);
                nvgRotate(vg, -0.5f * kPi);
                drawPlayerOverlay(vg, font, player, kHeight, kWidth);
                nvgRestore(vg);
            } else {
                drawPlayerOverlay(vg, font, player, kWidth, kHeight);
            }
        }
        return;
    }

    if (mode == ModeImage) {
        drawImageViewer(vg, font, image);
        return;
    }

    drawAppChrome(vg);

    if (mode == ModeConnect) {
        ui::drawTopBar(vg, font, t("app.title"), t("login.title"));

        float fx = 0.0f, fy = 0.0f, fw = 0.0f, fh = 0.0f;
        updateFocusRectMotion(&gConnectMotion, mode, connectFocus, &fx, &fy, &fw, &fh);
        ui::drawFocusPanel(vg, fx, fy, fw, fh, ui::kPanelRadius, pulse);

        char passwordMasked[64];
        maskPassword(gConn.password, passwordMasked, sizeof(passwordMasked));
        drawConnectRow(vg, font, 120.0f, connectFieldLabel(ConnectServer), gConn.server, connectFocus == ConnectServer);
        drawConnectRow(vg, font, 156.0f, connectFieldLabel(ConnectShare), gConn.share, connectFocus == ConnectShare);
        drawConnectRow(vg, font, 192.0f, connectFieldLabel(ConnectPath), gConn.path, connectFocus == ConnectPath);
        drawConnectRow(vg, font, 228.0f, connectFieldLabel(ConnectUser), gConn.user, connectFocus == ConnectUser);
        drawConnectRow(vg, font, 264.0f, connectFieldLabel(ConnectPassword), passwordMasked, connectFocus == ConnectPassword);
        drawConnectRow(vg, font, 300.0f, connectFieldLabel(ConnectDomain), gConn.domain, connectFocus == ConnectDomain);
        drawConnectRow(vg, font, 336.0f, connectFieldLabel(ConnectLocalPath), gConn.localPath, connectFocus == ConnectLocalPath);
        drawConnectActionButton(vg, font, 58.0f, 386.0f, 268.0f,
                                "START", t("connect.smb_button"), connectFocus == ConnectSmbAction);
        drawConnectActionButton(vg, font, 346.0f, 386.0f, 268.0f,
                                "SELECT", t("connect.local_button"), connectFocus == ConnectLocalAction);
        drawConnectActionButton(vg, font, 634.0f, 386.0f, 268.0f,
                                "L", t("connect.hidden_button"), connectFocus == ConnectHiddenAction);

        const FooterHint hints[] = {
            {"○", t("hint.edit")},
            {"START", t("connect.smb_button")},
            {"SELECT", t("connect.local_button")},
            {"L", t("connect.hidden_button")},
            {"×", t("hint.exit")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    if (mode == ModeHidden) {
        char countLine[128];
        std::snprintf(countLine, sizeof(countLine), t("hidden.count.fmt"), gHiddenItemCount);
        ui::drawTopBar(vg, font, t("hidden.title"), countLine);

        if (gHiddenItemCount > 0) {
            updateListMotion(&gHiddenMotion, mode, listTop, selected, gHiddenItemCount);
            nvgSave(vg);
            nvgIntersectScissor(vg, 0.0f, kListTopY - 8.0f, kWidth, ui::kFooterTop - kListTopY + 8.0f);
            const float focusTop = kListTopY + (gHiddenMotion.visualSelected - gHiddenMotion.visualTop) * kListRowPitch;
            drawListFocus(vg, focusTop, pulse);
            const int first = clampIndex(static_cast<int>(std::floor(gHiddenMotion.visualTop)) - 1, 0, gHiddenItemCount - 1);
            const int last = clampIndex(first + kVisibleEntries + 3, 0, gHiddenItemCount);
            for (int i = first; i < last; ++i) {
                const float top = kListTopY + (static_cast<float>(i) - gHiddenMotion.visualTop) * kListRowPitch;
                if (top > ui::kFooterTop || top + kListRowHeight < kListTopY - 8.0f) continue;
                drawHiddenRowAt(vg, font, gHiddenItems[i], top, i == selected);
            }
            nvgRestore(vg);
        } else {
            ui::drawGlassPanel(vg, 72.0f, 196.0f, 816.0f, 116.0f, ui::kPanelRadius, true);
            drawText(vg, font, 104.0f, 260.0f, 23.0f, ui::textPrimary(), t("hidden.empty"));
        }

        const FooterHint hints[] = {
            {"○", t("hint.unhide")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    if (mode == ModeExit) {
        ui::drawTopBar(vg, font, t("exit.title"), nullptr);
        ui::drawGlassPanel(vg, 72.0f, 154.0f, 816.0f, 190.0f, ui::kPanelRadius, true);
        drawText(vg, font, 104.0f, 210.0f, 23.0f, ui::textPrimary(), t("exit.message"));
        drawText(vg, font, 104.0f, 260.0f, 18.0f, ui::textSecondary(), t("exit.detail"));
        drawText(vg, font, 104.0f, 304.0f, 18.0f, ui::textSecondary(), t("exit.vita3k"));

        const FooterHint hints[] = {
            {"○", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    char shareLine[256];
    if (scan.source == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        std::snprintf(shareLine, sizeof(shareLine), t("local.prefix.fmt"), scan.path[0] ? scan.path : localRoot);
    } else {
        std::snprintf(shareLine, sizeof(shareLine), t("share.prefix.fmt"), gConn.server, gConn.share,
                      scan.path[0] ? scan.path : "");
    }
    ui::drawTopBar(vg, font, "", scan.message);
    drawMarqueeText(vg, font, 40.0f, 38.0f, 880.0f, 23.0f, ui::textPrimary(), shareLine, true);

    CopyState copy = {};
    lockScan();
    copy = gCopyState;
    unlockScan();
    drawCopyProgress(vg, font, copy);

    if (scan.phase == ScanReady && scan.count > 0) {
        updateListMotion(&gBrowserMotion, mode, listTop, selected, scan.count);
        nvgSave(vg);
        nvgIntersectScissor(vg, 0.0f, kListTopY - 8.0f, kWidth, ui::kFooterTop - kListTopY + 8.0f);
        const float focusTop = kListTopY + (gBrowserMotion.visualSelected - gBrowserMotion.visualTop) * kListRowPitch;
        drawListFocus(vg, focusTop, pulse);
        const int first = clampIndex(static_cast<int>(std::floor(gBrowserMotion.visualTop)) - 1, 0, scan.count - 1);
        const int last = clampIndex(first + kVisibleEntries + 3, 0, scan.count);
        for (int i = first; i < last; ++i) {
            const float top = kListTopY + (static_cast<float>(i) - gBrowserMotion.visualTop) * kListRowPitch;
            if (top > ui::kFooterTop || top + kListRowHeight < kListTopY - 8.0f) continue;
            drawEntryRowAt(vg, font, scan.entries[i], top, i == selected);
        }
        nvgRestore(vg);
    } else if (scan.phase == ScanReady) {
        ui::drawGlassPanel(vg, 72.0f, 196.0f, 816.0f, 116.0f, ui::kPanelRadius, true);
        drawText(vg, font, 104.0f, 260.0f, 23.0f, ui::textPrimary(), t("browser.empty"));
    } else if (scan.phase == ScanLoading) {
        ui::drawGlassPanel(vg, 72.0f, 196.0f, 816.0f, 116.0f, ui::kPanelRadius, true);
        drawText(vg, font, 104.0f, 260.0f, 23.0f, ui::textPrimary(), t("browser.wait"));
    }

    const bool hasSelectedEntry = scan.phase == ScanReady && selected >= 0 && selected < scan.count;
    const bool selectedDir = hasSelectedEntry && scan.entries[selected].directory;
    const bool selectedImage = hasSelectedEntry && !selectedDir && isImageFile(scan.entries[selected].name);
    if (!hasSelectedEntry) {
        const FooterHint hints[] = {
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else if (scan.source == SourceSmb && !selectedDir) {
        const FooterHint hints[] = {
            {"○", selectedImage ? t("hint.view") : t("hint.play")},
            {"□", t("hint.copy")},
            {"×", t("hint.back")},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else {
        const FooterHint hints[] = {
            {"○", selectedDir ? t("hint.enter") : (selectedImage ? t("hint.view") : t("hint.play"))},
            {"×", t("hint.back")},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
            {"△", t("hint.rescan")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    }
}
