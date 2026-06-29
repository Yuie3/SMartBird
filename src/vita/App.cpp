#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/mutex.h>
#include <psp2/io/stat.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>

#include "App.hpp"
#include "nanovg.h"
#ifndef SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE
#define SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE 0x805B0028
#endif
#include "nanovg_gxm.h"

#include "Browser/BrowserActions.hpp"
#include "Browser/BrowserState.hpp"
#include "Browser/CopyManager.hpp"
#include "Browser/Scanner.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Config/HiddenItems.hpp"
#include "Config/I18n.hpp"
#include "Config/Settings.hpp"
#include "Image/ImageViewer.hpp"
#include "Network/Runtime.hpp"
#include "Player/VideoPlayer.hpp"
#include "UI/ImeInput.hpp"
#include "UI/Input.hpp"
#include "UI/Screens.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Math.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>

namespace {

bool isPlayableVideoEntry(const SmbEntry& entry) {
    return !entry.directory && isVideoFile(entry.name);
}

int countPlayableVideos(const ScanState& scan) {
    int count = 0;
    if (scan.phase != ScanReady) return 0;
    for (int i = 0; i < scan.count; ++i) {
        if (isPlayableVideoEntry(scan.entries[i])) ++count;
    }
    return count;
}

int findPlaybackNeighbor(const ScanState& scan, int selected, int direction, bool wrap) {
    if (scan.phase != ScanReady || scan.count <= 0 || direction == 0) return -1;
    int i = selected;
    for (int steps = 0; steps < scan.count; ++steps) {
        i += direction > 0 ? 1 : -1;
        if (i < 0) {
            if (!wrap) return -1;
            i = scan.count - 1;
        } else if (i >= scan.count) {
            if (!wrap) return -1;
            i = 0;
        }
        if (isPlayableVideoEntry(scan.entries[i])) return i;
    }
    return -1;
}

int findRandomPlaybackIndex(const ScanState& scan, int selected) {
    const int playableCount = countPlayableVideos(scan);
    if (playableCount <= 0) return -1;
    if (playableCount == 1) return isPlayableVideoEntry(scan.entries[selected]) ? selected : findPlaybackNeighbor(scan, selected, 1, true);

    const int pick = gUiFrame % (playableCount - 1);
    int seen = 0;
    for (int i = 0; i < scan.count; ++i) {
        if (i == selected || !isPlayableVideoEntry(scan.entries[i])) continue;
        if (seen == pick) return i;
        ++seen;
    }
    return findPlaybackNeighbor(scan, selected, 1, true);
}

bool playBrowserVideoAt(const ScanState& scan, int index, int* selected, int* listTop, AppMode* mode,
                        NVGcontext* vg, SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
                        SceGxmMultisampleMode msaa) {
    if (!selected || !listTop || !mode) return false;
    if (scan.phase != ScanReady || index < 0 || index >= scan.count) return false;
    if (!isPlayableVideoEntry(scan.entries[index])) return false;
    *selected = index;
    keepSelectedVisible(*selected, scan.count, listTop);
    playEntry(scan.entries[index], scan.source, gxmCtx, patcher, msaa, vg);
    *mode = ModePlayer;
    return true;
}

} // namespace

int Application::run(SceSize, void*) {
    scePowerSetArmClockFrequency(444);
    scePowerSetBusClockFrequency(222);
    scePowerSetGpuClockFrequency(222);
    scePowerSetGpuXbarClockFrequency(166);

    SceAppUtilInitParam appInitParam = {};
    SceAppUtilBootParam appBootParam = {};
    sceAppUtilInit(&appInitParam, &appBootParam);
    initUiLanguageFromSystem();
    loadUiTranslationsFromResource();
    sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
    sceIoMkdir(kDataDir, 0777);
    initConnectionDefaults();
    loadConnectionConfig();
    loadHiddenItems();

    gScanMutex = sceKernelCreateMutex("smb_scan_mutex", 0, 1, nullptr);
    initCurrentPath();
    setScanMessage(ScanIdle, t("scan.idle"));

    RuntimeStatus runtime = probeLinkedLibraries();

    const bool networkOk = initNetwork();
    if (!networkOk) {
        setScanMessage(ScanError, t("scan.network_failed"));
    }

    NVGXMinitOptions gxmOpts = {};
    gxmOpts.msaa = SCE_GXM_MULTISAMPLE_NONE;
    gxmOpts.swapInterval = 1;
    gxmOpts.dumpShader = 0;
    gxmOpts.scenesPerFrame = 1;

    NVGXMwindow* window = gxmCreateWindow(&gxmOpts);
    if (!window) {
        sceClibPrintf("[SMartBird] gxmCreateWindow failed\n");
        if (networkOk) shutdownNetwork();
        return 1;
    }

    NVGcontext* vg = nvgCreateGXM(window->context, window->shader_patcher, NVG_STENCIL_STROKES);
    if (!vg) {
        sceClibPrintf("[SMartBird] nvgCreateGXM failed\n");
        gxmDeleteWindow(window);
        if (networkOk) shutdownNetwork();
        return 1;
    }

    int font = nvgCreateFont(vg, "sans", "app0:/CJK.ttf");
    if (font < 0) {
        sceClibPrintf("[SMartBird] CJK font failed, using Roboto\n");
        font = nvgCreateFont(vg, "sans", "app0:/Roboto-Regular.ttf");
        if (font < 0) {
            sceClibPrintf("[SMartBird] failed to load bundled font\n");
            nvgDeleteGXM(vg);
            gxmDeleteWindow(window);
            if (networkOk) shutdownNetwork();
            return 1;
        }
    }

    SceCommonDialogConfigParam commonCfg;
    sceCommonDialogConfigParamInit(&commonCfg);
    sceCommonDialogSetConfigParam(&commonCfg);

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    gxmClearColor(0.071f, 0.086f, 0.110f, 1.0f);

    SceCtrlData pad = {};
    SceCtrlData previousPad = {};
    bool quit = false;
    int selected = 0;
    int listTop = 0;
    int connectFocus = ConnectServer;
    int upHoldFrames = 0;
    int downHoldFrames = 0;
    int leftHoldFrames = 0;
    int rightHoldFrames = 0;
    int actionPulseFrames = 0;
    AppMode mode = ModeConnect;
    AppMode hiddenReturnMode = ModeConnect;
    int hiddenReturnSelected = 0;
    int hiddenReturnListTop = 0;
    bool pendingBrowserFocusRestore = false;
    int pendingBrowserSelected = 0;
    int pendingBrowserListTop = 0;
    bool listTouchFocusFrozen = false;

    while (!quit) {
        ++gUiFrame;
        sceCtrlPeekBufferPositive(0, &pad, 1);
        SceTouchData touch = {};
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1);
        const bool touching = touch.reportNum > 0;
        const float touchX = touching ? touch.report[0].x * 0.5f : 0.0f;
        const float touchY = touching ? touch.report[0].y * 0.5f : 0.0f;
        const bool multiTouching = touch.reportNum > 1;
        const float touch2X = multiTouching ? touch.report[1].x * 0.5f : 0.0f;
        const float touch2Y = multiTouching ? touch.report[1].y * 0.5f : 0.0f;
        const bool uiTouchStarted = !gUiTouchPrev && touching;
        const bool uiTouchEnded = gUiTouchPrev && !touching;
        if (uiTouchStarted) {
            gUiTouchStartX = touchX;
            gUiTouchStartY = touchY;
            gUiTouchLastX = touchX;
            gUiTouchLastY = touchY;
            gUiTouchScrollCarry = 0.0f;
            gUiPinchPrev = false;
            gUiTouchGesture = false;
            listTouchFocusFrozen = false;
        }

        pollMpvEvents();
        if (mode == ModePlayer) {
            updateMpvAutoRotation();
        }
        updateImeDialog();

        ScanState snapshot = {};
        PlayerState playerSnapshot = {};
        lockScan();
        snapshot = gScanState;
        playerSnapshot = gPlayer;
        int pendingPlayerAction = PlayerPendingNone;
        const bool playbackEnded = mode == ModePlayer && gPlayer.playbackEnded != 0;
        if (mode == ModePlayer) {
            pendingPlayerAction = gPlayerPendingAction;
            gPlayerPendingAction = PlayerPendingNone;
            gPlayer.playbackEnded = 0;
        }
        unlockScan();

        if (mode == ModePlayer && snapshot.phase == ScanReady) {
            int playbackTarget = -1;
            if (pendingPlayerAction == PlayerPendingPrevious) {
                playbackTarget = findPlaybackNeighbor(snapshot, selected, -1, true);
            } else if (pendingPlayerAction == PlayerPendingNext) {
                playbackTarget = playerSnapshot.shufflePlayback
                    ? findRandomPlaybackIndex(snapshot, selected)
                    : findPlaybackNeighbor(snapshot, selected, 1, true);
            } else if (playbackEnded &&
                       (playerSnapshot.repeatMode == PlayerRepeatAll || playerSnapshot.shufflePlayback)) {
                playbackTarget = playerSnapshot.shufflePlayback
                    ? findRandomPlaybackIndex(snapshot, selected)
                    : findPlaybackNeighbor(snapshot, selected, 1, true);
            }

            if (playbackTarget >= 0) {
                playBrowserVideoAt(snapshot, playbackTarget, &selected, &listTop, &mode, vg,
                                   window->context, window->shader_patcher,
                                   static_cast<SceGxmMultisampleMode>(SCE_GXM_MULTISAMPLE_NONE));
                lockScan();
                playerSnapshot = gPlayer;
                unlockScan();
            }
        }

        bool consumedCross = false;
        bool browserStartedScanThisFrame = false;
        const bool upAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_UP, &upHoldFrames);
        const bool downAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_DOWN, &downHoldFrames);
        const bool leftAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_LEFT, &leftHoldFrames);
        const bool rightAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_RIGHT, &rightHoldFrames);
        const bool openHiddenPressed = !gImeOpen &&
            (mode == ModeConnect || mode == ModeBrowser) &&
            pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LTRIGGER);
        const float uiTouchDx = gUiTouchLastX - gUiTouchStartX;
        const float uiTouchDy = gUiTouchLastY - gUiTouchStartY;
        const bool uiTouchTap = uiTouchEnded && uiTouchDx * uiTouchDx + uiTouchDy * uiTouchDy < 24.0f * 24.0f;
        if (mode != ModeBrowser) {
            gUiBackSwipeActive = 0;
            gUiBackSwipeDirection = 0;
            gUiBackSwipeProgress = 0.0f;
        } else if (!touching && !gUiBackSwipeActive && gUiBackSwipeProgress > 0.0f) {
            gUiBackSwipeProgress *= 0.70f;
            if (gUiBackSwipeProgress < 0.01f) {
                gUiBackSwipeProgress = 0.0f;
                gUiBackSwipeDirection = 0;
            }
        }
        const bool actionPressed = !gImeOpen &&
            (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_START) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SELECT) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SQUARE) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_TRIANGLE) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LTRIGGER) ||
             pressed(pad.buttons, previousPad.buttons, SCE_CTRL_RTRIGGER) ||
             uiTouchTap);
        if (actionPressed) {
            actionPulseFrames = 8;
        } else if (actionPulseFrames > 0) {
            --actionPulseFrames;
        }

        if (gImeOpen) {
            consumedCross = true;
        } else if (openHiddenPressed) {
            hiddenReturnMode = mode;
            hiddenReturnSelected = selected;
            hiddenReturnListTop = listTop;
            selected = 0;
            listTop = 0;
            mode = ModeHidden;
        } else if (mode == ModeConnect) {
            if (connectFocus <= ConnectLocalPath) {
                if (upAction && connectFocus > 0) --connectFocus;
                if (downAction) {
                    connectFocus = connectFocus == ConnectLocalPath ? ConnectSmbAction : connectFocus + 1;
                }
            } else {
                if (upAction) connectFocus = ConnectLocalPath;
                if (leftAction && connectFocus > ConnectSmbAction) --connectFocus;
                if (rightAction && connectFocus < ConnectHiddenAction) ++connectFocus;
            }

            const bool connectPressed = pressed(pad.buttons, previousPad.buttons, SCE_CTRL_START) ||
                (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) && connectFocus == ConnectSmbAction);
            if (connectPressed && networkOk) {
                saveConnectionConfig();
                gBrowserSource = SourceSmb;
                initCurrentPath();
                selected = 0;
                listTop = 0;
                startScan();
                mode = ModeBrowser;
            } else if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                       connectFocus >= ConnectServer && connectFocus <= ConnectLocalPath) {
                openImeForField(connectFocus);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                connectFocus == ConnectHiddenAction) {
                hiddenReturnMode = ModeConnect;
                hiddenReturnSelected = 0;
                hiddenReturnListTop = 0;
                selected = 0;
                listTop = 0;
                mode = ModeHidden;
            }
            if ((pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SELECT) ||
                 (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) && connectFocus == ConnectLocalAction))) {
                saveConnectionConfig();
                gBrowserSource = SourceLocal;
                initLocalPath();
                selected = 0;
                listTop = 0;
                startScan();
                mode = ModeBrowser;
            }
            if (uiTouchEnded) {
                const float dx = gUiTouchLastX - gUiTouchStartX;
                const float dy = gUiTouchLastY - gUiTouchStartY;
                if (dx * dx + dy * dy < 24.0f * 24.0f) {
                    const int hit = connectFieldAtPoint(gUiTouchLastX, gUiTouchLastY);
                    if (hit >= ConnectServer && hit <= ConnectLocalPath) {
                        connectFocus = hit;
                        openImeForField(hit);
                    } else if (hit == ConnectSmbAction && networkOk) {
                        connectFocus = hit;
                        saveConnectionConfig();
                        gBrowserSource = SourceSmb;
                        initCurrentPath();
                        selected = 0;
                        listTop = 0;
                        startScan();
                        mode = ModeBrowser;
                    } else if (hit == ConnectLocalAction) {
                        connectFocus = hit;
                        saveConnectionConfig();
                        gBrowserSource = SourceLocal;
                        initLocalPath();
                        selected = 0;
                        listTop = 0;
                        startScan();
                        mode = ModeBrowser;
                    } else if (hit == ConnectHiddenAction) {
                        connectFocus = hit;
                        hiddenReturnMode = ModeConnect;
                        hiddenReturnSelected = 0;
                        hiddenReturnListTop = 0;
                        selected = 0;
                        listTop = 0;
                        mode = ModeHidden;
                    }
                }
            }
        } else if (mode == ModeHidden) {
            if (upAction && selected > 0) {
                --selected;
                keepSelectedVisible(selected, gHiddenItemCount, &listTop);
            }
            if (downAction && selected + 1 < gHiddenItemCount) {
                ++selected;
                keepSelectedVisible(selected, gHiddenItemCount, &listTop);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                selected < gHiddenItemCount) {
                removeHiddenItemAt(selected);
                if (selected >= gHiddenItemCount) selected = gHiddenItemCount > 0 ? gHiddenItemCount - 1 : 0;
                keepSelectedVisible(selected, gHiddenItemCount, &listTop);
            }
            if (uiTouchStarted && !multiTouching) {
                const int hit = listIndexAtPoint(touchX, touchY, listTop, gHiddenItemCount);
                if (hit >= 0) {
                    selected = hit;
                    gUiSnapListMotionFrame = gUiFrame;
                }
            }
            if (touching && gUiTouchPrev && !multiTouching) {
                const float dragY = touchY - gUiTouchLastY;
                const int maxTop = gHiddenItemCount > kVisibleEntries ? gHiddenItemCount - kVisibleEntries : 0;
                const bool blockedDrag = (dragY > 0.0f && listTop <= 0) || (dragY < 0.0f && listTop >= maxTop);
                if (blockedDrag) {
                    gUiTouchScrollCarry = 0.0f;
                    listTouchFocusFrozen = true;
                } else {
                    gUiTouchScrollCarry += -dragY;
                    const int rows = static_cast<int>(gUiTouchScrollCarry / kListRowPitch);
                    if (rows != 0) {
                        if (listTouchFocusFrozen) {
                            listTop += rows;
                            if (listTop < 0) listTop = 0;
                            if (listTop > maxTop) listTop = maxTop;
                        } else {
                            scrollListByRows(rows, gHiddenItemCount, &selected, &listTop);
                        }
                        gUiTouchScrollCarry -= rows * kListRowPitch;
                    }
                    if (!listTouchFocusFrozen) {
                        const int hit = listIndexAtPoint(touchX, touchY, listTop, gHiddenItemCount);
                        if (hit >= 0) selected = hit;
                    }
                }
            }
            if (uiTouchEnded) {
                const float dx = gUiTouchLastX - gUiTouchStartX;
                const float dy = gUiTouchLastY - gUiTouchStartY;
                if (dx * dx + dy * dy < 24.0f * 24.0f) {
                    const int hit = listIndexAtPoint(gUiTouchLastX, gUiTouchLastY, listTop, gHiddenItemCount);
                    if (hit >= 0) {
                        if (hit == selected) {
                            removeHiddenItemAt(selected);
                            if (selected >= gHiddenItemCount) selected = gHiddenItemCount > 0 ? gHiddenItemCount - 1 : 0;
                        } else {
                            selected = hit;
                        }
                        keepSelectedVisible(selected, gHiddenItemCount, &listTop);
                    }
                }
            }
        } else if (mode == ModeBrowser) {
            if (upAction && selected > 0) {
                --selected;
                keepSelectedNearListCenter(selected, snapshot.count, &listTop);
            }
            if (downAction && selected + 1 < snapshot.count) {
                ++selected;
                keepSelectedNearListCenter(selected, snapshot.count, &listTop);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_TRIANGLE) &&
                (gBrowserSource == SourceLocal || networkOk)) {
                startScan();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SELECT) &&
                snapshot.phase == ScanReady && selected < snapshot.count) {
                if (snapshot.source == SourceSmb) {
                    addHiddenItem(snapshot.source, gConn.server, gConn.share,
                                  snapshot.path, snapshot.entries[selected].name);
                } else {
                    addHiddenItem(snapshot.source, "", "", snapshot.path, snapshot.entries[selected].name);
                }
                if (selected > 0 && selected + 1 >= snapshot.count) --selected;
                keepSelectedVisible(selected, snapshot.count > 0 ? snapshot.count - 1 : 0, &listTop);
                startScan();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SQUARE) &&
                snapshot.source == SourceSmb && networkOk &&
                snapshot.phase == ScanReady && selected < snapshot.count &&
                !snapshot.entries[selected].directory) {
                startCopySelected(snapshot.entries[selected], snapshot.source);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                snapshot.phase == ScanReady && selected < snapshot.count) {
                openBrowserEntryAtIndex(snapshot, selected, &selected, &listTop, &mode, vg,
                                        window->context, window->shader_patcher,
                                        static_cast<SceGxmMultisampleMode>(SCE_GXM_MULTISAMPLE_NONE));
            }
            if (uiTouchStarted && !multiTouching && snapshot.phase == ScanReady) {
                const int hit = listIndexAtPoint(touchX, touchY, listTop, snapshot.count);
                if (hit >= 0) {
                    selected = hit;
                    gUiSnapListMotionFrame = gUiFrame;
                }
            }
            if (touching && gUiTouchPrev && !multiTouching) {
                const float swipeDx = touchX - gUiTouchStartX;
                const float swipeDy = touchY - gUiTouchStartY;
                const float absSwipeDy = swipeDy >= 0.0f ? swipeDy : -swipeDy;
                const bool edgeSwipeTracking = gUiTouchStartX <= 36.0f && swipeDx > 0.0f && swipeDx > absSwipeDy * 1.15f;
                const bool edgeForwardTracking = gUiTouchStartX >= kWidth - 36.0f && swipeDx < 0.0f &&
                    -swipeDx > absSwipeDy * 1.15f && hasForwardDirectory();
                if (edgeSwipeTracking || edgeForwardTracking) {
                    gUiBackSwipeActive = 1;
                    gUiBackSwipeDirection = edgeForwardTracking ? -1 : 1;
                    gUiBackSwipeProgress = clampFloat((edgeForwardTracking ? -swipeDx : swipeDx) / 160.0f, 0.0f, 1.0f);
                    gUiTouchGesture = true;
                }
            }
            if (touching && gUiTouchPrev && !multiTouching && !gUiBackSwipeActive) {
                const float dragY = touchY - gUiTouchLastY;
                const int maxTop = snapshot.count > kVisibleEntries ? snapshot.count - kVisibleEntries : 0;
                const bool blockedDrag = (dragY > 0.0f && listTop <= 0) || (dragY < 0.0f && listTop >= maxTop);
                if (blockedDrag) {
                    gUiTouchScrollCarry = 0.0f;
                    listTouchFocusFrozen = true;
                } else {
                    gUiTouchScrollCarry += -dragY;
                    const int rows = static_cast<int>(gUiTouchScrollCarry / kListRowPitch);
                    if (rows != 0) {
                        if (listTouchFocusFrozen) {
                            listTop += rows;
                            if (listTop < 0) listTop = 0;
                            if (listTop > maxTop) listTop = maxTop;
                        } else {
                            scrollListByRows(rows, snapshot.count, &selected, &listTop);
                        }
                        gUiTouchScrollCarry -= rows * kListRowPitch;
                    }
                    if (!listTouchFocusFrozen) {
                        const int hit = listIndexAtPoint(touchX, touchY, listTop, snapshot.count);
                        if (hit >= 0) selected = hit;
                    }
                }
            }
            if (uiTouchEnded) {
                const float dx = gUiTouchLastX - gUiTouchStartX;
                const float dy = gUiTouchLastY - gUiTouchStartY;
                const float absDy = dy >= 0.0f ? dy : -dy;
                const bool edgeSwipeBack = gUiTouchStartX <= 36.0f && dx >= 92.0f && dx > absDy * 1.45f;
                const bool edgeSwipeForward = gUiTouchStartX >= kWidth - 36.0f && dx <= -92.0f &&
                    -dx > absDy * 1.45f && hasForwardDirectory();
                if (edgeSwipeBack) {
                    gUiBackSwipeActive = 0;
                    gUiBackSwipeDirection = 0;
                    gUiBackSwipeProgress = 0.0f;
                    if (goParentDirectoryAndRememberForward(selected, listTop)) {
                        if (popBrowserFocus(&pendingBrowserSelected, &pendingBrowserListTop)) {
                            pendingBrowserFocusRestore = true;
                        } else {
                            selected = 0;
                            listTop = 0;
                            pendingBrowserFocusRestore = false;
                        }
                        startScan();
                        browserStartedScanThisFrame = true;
                    } else {
                        mode = ModeConnect;
                    }
                    consumedCross = true;
                } else if (edgeSwipeForward) {
                    gUiBackSwipeActive = 0;
                    gUiBackSwipeDirection = 0;
                    gUiBackSwipeProgress = 0.0f;
                    if (goForwardDirectory(&pendingBrowserSelected, &pendingBrowserListTop)) {
                        pendingBrowserFocusRestore = true;
                        selected = 0;
                        listTop = 0;
                        startScan();
                        browserStartedScanThisFrame = true;
                    }
                } else if (dx * dx + dy * dy < 24.0f * 24.0f) {
                    gUiBackSwipeActive = 0;
                    gUiBackSwipeDirection = 0;
                    const int hit = listIndexAtPoint(gUiTouchLastX, gUiTouchLastY, listTop, snapshot.count);
                    if (hit >= 0) {
                        openBrowserEntryAtIndex(snapshot, hit, &selected, &listTop, &mode, vg,
                                                window->context, window->shader_patcher,
                                                static_cast<SceGxmMultisampleMode>(SCE_GXM_MULTISAMPLE_NONE));
                    }
                } else {
                    gUiBackSwipeActive = 0;
                    gUiBackSwipeDirection = 0;
                }
            }
        } else if (mode == ModeImage) {
            const float lx = analogAxis(pad.lx);
            const float ly = analogAxis(pad.ly);
            if (lx != 0.0f || ly != 0.0f) {
                panImage(-lx * 8.0f, -ly * 8.0f);
            }
            if (multiTouching) {
                const float dx = touch2X - touchX;
                const float dy = touch2Y - touchY;
                const float distance = std::sqrt(dx * dx + dy * dy);
                const float centerX = (touchX + touch2X) * 0.5f;
                const float centerY = (touchY + touch2Y) * 0.5f;
                if (gUiPinchPrev && gUiPinchDistance > 8.0f && distance > 8.0f) {
                    zoomImage(distance / gUiPinchDistance);
                    panImage(centerX - gUiPinchCenterX, centerY - gUiPinchCenterY);
                }
                gUiPinchPrev = true;
                gUiPinchDistance = distance;
                gUiPinchCenterX = centerX;
                gUiPinchCenterY = centerY;
                gUiTouchGesture = true;
            } else {
                gUiPinchPrev = false;
            }
            if (touching && gUiTouchPrev && !multiTouching) {
                const float dx = touchX - gUiTouchLastX;
                const float dy = touchY - gUiTouchLastY;
                if (dx != 0.0f || dy != 0.0f) {
                    panImage(dx, dy);
                    if ((touchX - gUiTouchStartX) * (touchX - gUiTouchStartX) +
                        (touchY - gUiTouchStartY) * (touchY - gUiTouchStartY) > 20.0f * 20.0f) {
                        gUiTouchGesture = true;
                    }
                }
            }

            const float ry = analogAxis(pad.ry);
            if (ry < 0.0f) zoomImage(1.0f + (-ry * 0.045f));
            if (ry > 0.0f) zoomImage(1.0f / (1.0f + (ry * 0.045f)));
            if (pad.buttons & SCE_CTRL_UP) zoomImage(1.035f);
            if (pad.buttons & SCE_CTRL_DOWN) zoomImage(1.0f / 1.035f);

            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE)) {
                resetImageView();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_TRIANGLE)) {
                rotateImageClockwise();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SQUARE)) {
                toggleImageHud();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LTRIGGER)) {
                const int next = findAdjacentImageIndex(snapshot, selected, -1);
                if (next >= 0 && openImageEntry(snapshot.entries[next], snapshot.source, vg, true)) {
                    selected = next;
                    keepSelectedVisible(selected, snapshot.count, &listTop);
                }
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_RTRIGGER)) {
                const int next = findAdjacentImageIndex(snapshot, selected, 1);
                if (next >= 0 && openImageEntry(snapshot.entries[next], snapshot.source, vg, true)) {
                    selected = next;
                    keepSelectedVisible(selected, snapshot.count, &listTop);
                }
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
                closeImage(vg);
                mode = ModeBrowser;
                consumedCross = true;
            }
            if (uiTouchEnded && !gUiTouchGesture) {
                const float dx = gUiTouchLastX - gUiTouchStartX;
                const float dy = gUiTouchLastY - gUiTouchStartY;
                if (dx * dx + dy * dy < 20.0f * 20.0f) {
                    if (gUiTouchLastY < 70.0f && gUiTouchLastX < 110.0f) {
                        closeImage(vg);
                        mode = ModeBrowser;
                        consumedCross = true;
                    } else if (gUiTouchLastX < 120.0f) {
                        const int next = findAdjacentImageIndex(snapshot, selected, -1);
                        if (next >= 0 && openImageEntry(snapshot.entries[next], snapshot.source, vg, true)) {
                            selected = next;
                            keepSelectedVisible(selected, snapshot.count, &listTop);
                        }
                    } else if (gUiTouchLastX > kWidth - 120.0f) {
                        const int next = findAdjacentImageIndex(snapshot, selected, 1);
                        if (next >= 0 && openImageEntry(snapshot.entries[next], snapshot.source, vg, true)) {
                            selected = next;
                            keepSelectedVisible(selected, snapshot.count, &listTop);
                        }
                    } else {
                        resetImageView();
                    }
                }
            }
        } else {
            if (!playerSnapshot.settingsVisible &&
                pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                !playerSnapshot.waitingForValidation) {
                showPlayerOverlay();
                setMpvPause(!gPlayer.paused);
            }
            if (!playerSnapshot.settingsVisible &&
                pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SQUARE)) {
                togglePlayerOverlay();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_TRIANGLE)) {
                toggleMpvAutoRotate();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_SELECT)) {
                toggleMpvLoop();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_START)) {
                togglePlayerSettingsPanel();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LTRIGGER)) {
                adjustMpvSpeed(-1);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_RTRIGGER)) {
                adjustMpvSpeed(1);
            }
            if (!playerSnapshot.settingsVisible &&
                pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LEFT)) {
                showPlayerOverlay();
                seekMpvRelative(-10.0);
            }
            if (!playerSnapshot.settingsVisible &&
                pressed(pad.buttons, previousPad.buttons, SCE_CTRL_RIGHT)) {
                showPlayerOverlay();
                seekMpvRelative(10.0);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
                if (playerSnapshot.settingsVisible) {
                    togglePlayerSettingsPanel();
                } else {
                    stopCurrentPlayback();
                    mode = ModeBrowser;
                }
                consumedCross = true;
            }
            if (handlePlayerTouch(touching, touchX, touchY, vg, font)) {
                stopCurrentPlayback();
                mode = ModeBrowser;
            }
        }

        if (mode != ModePlayer) {
            gTouchPrev = false;
            gTouchDraggingBar = false;
            gTouchDraggingSpeed = false;
            gTouchSwipeSeeking = false;
            gTouchStartedOnPlayerControl = false;
            gTouchHudAction = 0;
            lockScan();
            gPlayer.swipeSeeking = 0;
            unlockScan();
        }
        if (mode == ModePlayer) {
            gUiTouchPrev = false;
            gUiPinchPrev = false;
        } else {
            if (touching) {
                gUiTouchLastX = touchX;
                gUiTouchLastY = touchY;
            } else {
                gUiPinchPrev = false;
            }
            gUiTouchPrev = touching;
        }

        if (mode == ModeBrowser) {
            if (pendingBrowserFocusRestore && !browserStartedScanThisFrame && snapshot.phase != ScanLoading) {
                if (snapshot.phase == ScanReady) {
                    selected = pendingBrowserSelected;
                    listTop = pendingBrowserListTop;
                } else {
                    selected = 0;
                    listTop = 0;
                }
                pendingBrowserFocusRestore = false;
            }
            if (selected >= snapshot.count) selected = snapshot.count > 0 ? snapshot.count - 1 : 0;
            keepSelectedVisible(selected, snapshot.count, &listTop);
        } else if (mode == ModeHidden) {
            if (selected >= gHiddenItemCount) selected = gHiddenItemCount > 0 ? gHiddenItemCount - 1 : 0;
            keepSelectedVisible(selected, gHiddenItemCount, &listTop);
        }

        if (!gImeOpen && mode == ModeConnect && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
            mode = ModeExit;
        } else if (!gImeOpen && mode == ModeExit && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE)) {
            mode = ModeConnect;
        } else if (!gImeOpen && mode == ModeHidden && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
            mode = hiddenReturnMode;
            selected = hiddenReturnMode == ModeBrowser ? hiddenReturnSelected : 0;
            listTop = hiddenReturnMode == ModeBrowser ? hiddenReturnListTop : 0;
        } else if (!gImeOpen && mode == ModeBrowser && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS) && !consumedCross) {
            if (goParentDirectoryAndRememberForward(selected, listTop)) {
                if (popBrowserFocus(&pendingBrowserSelected, &pendingBrowserListTop)) {
                    pendingBrowserFocusRestore = true;
                } else {
                    selected = 0;
                    listTop = 0;
                    pendingBrowserFocusRestore = false;
                }
                startScan();
                browserStartedScanThisFrame = true;
            } else {
                mode = ModeConnect;
            }
        }

        previousPad = pad;

        if (mode == ModePlayer) {
            tickPlayerOverlay();
        }
        if (mode == ModePlayer && !gPlayer.paused) {
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        }
        renderMpvToFbo(mode == ModePlayer);

        gxmBeginFrame();
        gxmClear();

        nvgBeginFrame(vg, kWidth, kHeight, 1.0f);
        ImageState imageSnapshot = {};
        lockScan();
        imageSnapshot = gImage;
        unlockScan();
        renderUi(vg, font, runtime, snapshot, playerSnapshot, imageSnapshot,
                 mode, selected, listTop, connectFocus, actionPulseFrames);
        nvgEndFrame(vg);

        gxmEndFrame();
        if (gImeOpen) {
            const unsigned int back = window->fb->gxm_back_buffer_index;
            SceCommonDialogUpdateParam dlgParam;
            std::memset(&dlgParam, 0, sizeof(dlgParam));
            dlgParam.renderTarget.colorFormat = DISPLAY_COLOR_FORMAT;
            dlgParam.renderTarget.surfaceType = DISPLAY_COLOR_SURFACE_TYPE;
            dlgParam.renderTarget.width = DISPLAY_WIDTH;
            dlgParam.renderTarget.height = DISPLAY_HEIGHT;
            dlgParam.renderTarget.strideInPixels = DISPLAY_STRIDE;
            dlgParam.renderTarget.colorSurfaceData = window->fb->gxm_color_surfaces[back].surface_addr;
            dlgParam.renderTarget.depthSurfaceData = window->fb->gxm_depth_stencil_surface_addr;
            dlgParam.displaySyncObject = window->fb->gxm_color_surfaces[back].sync_object;
            sceCommonDialogUpdate(&dlgParam);
        }
        gxmSwapBuffer();
    }

    // Vita3K 0.2.1 on macOS can crash while tearing down emulated threads,
    // GXM, and logger state during app exit. Let the process cleanup path own
    // final resource release instead of explicitly destroying subsystems here.
    return 0;

    bool scanStillRunning = false;
    bool copyStillRunning = false;
    lockScan();
    scanStillRunning = gScanState.phase == ScanLoading;
    copyStillRunning = gCopyState.busy != 0;
    unlockScan();

    shutdownMpv();
    closeImage(vg);
    nvgDeleteGXM(vg);
    gxmDeleteWindow(window);
    if (!scanStillRunning && !copyStillRunning) {
        if (networkOk) shutdownNetwork();
        if (gScanMutex >= 0) sceKernelDeleteMutex(gScanMutex);
    }
    sceAppUtilShutdown();

    return 0;
}

int appMain(SceSize args, void* arg) {
    Application app;
    return app.run(args, arg);
}
