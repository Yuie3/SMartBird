#include "Core/State.hpp"

SceUID gScanMutex = -1;
ScanState gScanState = {};
CopyState gCopyState = {};
PlayerState gPlayer = {};
ImageState gImage = {};
ConnectionConfig gConn = {};
char gCurrentPath[256] = {};
BrowserHistoryEntry gBrowserHistory[kMaxBrowserHistory] = {};
int gBrowserHistoryCount = 0;
HiddenItem gHiddenItems[kMaxHiddenItems] = {};
int gHiddenItemCount = 0;
int gBrowserSource = SourceSmb;
int gUiFrame = 0;
void* gNetMem = nullptr;
bool gTouchPrev = false;
bool gTouchDraggingBar = false;
bool gTouchDraggingSpeed = false;
bool gTouchSwipeSeeking = false;
bool gTouchStartedOnPlayerControl = false;
int gTouchHudAction = 0;
float gTouchStartX = 0.0f;
float gTouchStartY = 0.0f;
float gTouchLastX = 0.0f;
float gTouchLastY = 0.0f;
double gTouchScrubTarget = 0.0;
double gTouchSwipeSeekStart = 0.0;
double gTouchSwipeSeekTarget = 0.0;
bool gUiTouchPrev = false;
float gUiTouchStartX = 0.0f;
float gUiTouchStartY = 0.0f;
float gUiTouchLastX = 0.0f;
float gUiTouchLastY = 0.0f;
float gUiTouchScrollCarry = 0.0f;
bool gUiPinchPrev = false;
float gUiPinchDistance = 0.0f;
float gUiPinchCenterX = 0.0f;
float gUiPinchCenterY = 0.0f;
bool gUiTouchGesture = false;
bool gImeOpen = false;
int gImeField = -1;
SceWChar16 gImeBuffer[256] = {};
SceWChar16 gImeTitle[64] = {};

void lockScan() {
    if (gScanMutex >= 0) sceKernelLockMutex(gScanMutex, 1, nullptr);
}

void unlockScan() {
    if (gScanMutex >= 0) sceKernelUnlockMutex(gScanMutex, 1);
}
