#pragma once

#include "Core/Constants.hpp"

extern "C" {
#include <mpv/render.h>
#include <mpv/render_gxm.h>
}

#include <cstdint>

struct NVGcontext;
struct NVGXMframebuffer;
struct smb2_context;
struct smb2fh;

enum ScanPhase {
    ScanIdle = 0,
    ScanLoading,
    ScanReady,
    ScanError,
};

enum AppMode {
    ModeConnect = 0,
    ModeBrowser,
    ModePlayer,
    ModeImage,
    ModeHidden,
    ModeError,
};

enum BrowserSource {
    SourceSmb = 0,
    SourceLocal,
};

enum ConnectField {
    ConnectServer = 0,
    ConnectShare,
    ConnectPath,
    ConnectUser,
    ConnectPassword,
    ConnectDomain,
    ConnectLocalPath,
    ConnectSmbAction,
    ConnectLocalAction,
    ConnectHiddenAction,
    ConnectFieldCount,
};

struct RuntimeStatus {
    bool smb2ContextOk = false;
    unsigned long mpvClientApi = 0;
};

struct SmbEntry {
    char name[192];
    uint64_t size;
    int directory;
};

struct ScanState {
    int phase;
    int source;
    int count;
    char message[192];
    char path[256];
    SmbEntry entries[kMaxEntries];
};

struct CopyState {
    int busy;
    int done;
    int error;
    uint64_t copied;
    uint64_t total;
    char fileName[192];
    char destPath[256];
    char message[192];
};

struct CopyJob {
    char smbPath[256];
    char fileName[192];
    char destDir[256];
    char destPath[256];
    uint64_t total;
};

struct BrowserHistoryEntry {
    char path[256];
    int selected;
    int listTop;
};

struct HiddenItem {
    int source;
    char server[128];
    char share[64];
    char path[256];
    char name[192];
};

struct PlayerState {
    mpv_handle* mpv;
    mpv_render_context* renderCtx;
    NVGXMframebuffer* fbo;
    mpv_gxm_fbo mpvFbo;
    mpv_render_param mpvParams[3];
    NVGcontext* vg;
    int nvgImage;
    int flipY;
    int initialized;
    int loading;
    int hasFrame;
    int paused;
    double positionSeconds;
    double durationSeconds;
    char fileName[192];
    char message[192];
    char detail[192];
    char errorTitle[96];
    char errorReason[256];
    char videoCodec[64];
    char audioCodec[64];
    int videoWidth;
    int videoHeight;
    int rotationDegrees;
    int waitingForValidation;
    char logLines[kPlayerLogLines][128];
    int logCount;
    int logScroll;
    int overlayFrames;
};

struct ImageState {
    int nvgImage;
    int loaded;
    int error;
    int width;
    int height;
    int rotationDegrees;
    int hudVisible;
    float zoom;
    float offsetX;
    float offsetY;
    char fileName[192];
    char message[192];
};

struct SmbStream {
    struct smb2_context* smb;
    struct smb2fh* fh;
    int64_t size;
    int cancelled;
};

struct ConnectionConfig {
    char server[128];
    char share[64];
    char path[256];
    char user[64];
    char password[64];
    char domain[64];
    char localPath[256];
};
