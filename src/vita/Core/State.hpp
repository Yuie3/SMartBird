#pragma once

#include "Core/Types.hpp"

#include <psp2/ime_dialog.h>
#include <psp2/kernel/threadmgr.h>

extern SceUID gScanMutex;
extern ScanState gScanState;
extern CopyState gCopyState;
extern PlayerState gPlayer;
extern ImageState gImage;
extern ConnectionConfig gConn;
extern char gCurrentPath[256];
extern BrowserHistoryEntry gBrowserHistory[kMaxBrowserHistory];
extern int gBrowserHistoryCount;
extern HiddenItem gHiddenItems[kMaxHiddenItems];
extern int gHiddenItemCount;
extern int gBrowserSource;
extern int gUiFrame;
extern void* gNetMem;
extern bool gTouchPrev;
extern bool gTouchDraggingBar;
extern bool gTouchDraggingSpeed;
extern bool gTouchSwipeSeeking;
extern bool gTouchStartedOnPlayerControl;
extern int gTouchHudAction;
extern float gTouchStartX;
extern float gTouchStartY;
extern float gTouchLastX;
extern float gTouchLastY;
extern double gTouchScrubTarget;
extern double gTouchSwipeSeekStart;
extern double gTouchSwipeSeekTarget;
extern bool gUiTouchPrev;
extern float gUiTouchStartX;
extern float gUiTouchStartY;
extern float gUiTouchLastX;
extern float gUiTouchLastY;
extern float gUiTouchScrollCarry;
extern bool gUiPinchPrev;
extern float gUiPinchDistance;
extern float gUiPinchCenterX;
extern float gUiPinchCenterY;
extern bool gUiTouchGesture;
extern bool gImeOpen;
extern int gImeField;
extern SceWChar16 gImeBuffer[256];
extern SceWChar16 gImeTitle[64];

void lockScan();
void unlockScan();
