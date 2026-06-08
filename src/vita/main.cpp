#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/ctrl.h>
#include <psp2/gxm.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/mutex.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/ime_dialog.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/power.h>
#include <psp2/system_param.h>
#include <psp2/sysmodule.h>
#include <psp2/touch.h>

extern "C" {
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gxm.h>
#include <mpv/stream_cb.h>
#include <stddef.h>
#include <stdint.h>
#include <fcntl.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include "SmbConfig.hpp"
#include "nanovg.h"
#ifndef SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE
#define SCE_GXM_ERROR_INVALID_AUXILIARY_SURFACE 0x805B0028
#endif
#include "nanovg_gxm.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <cmath>

unsigned int _newlib_heap_size_user = 192 * 1024 * 1024;
unsigned int _pthread_stack_default_user = 2 * 1024 * 1024;

namespace {

constexpr float kWidth = 960.0f;
constexpr float kHeight = 544.0f;
constexpr float kPi = 3.14159265f;
constexpr int kNetMemSize = 1 * 1024 * 1024;
constexpr int kMaxEntries = 64;
constexpr int kVisibleEntries = 8;
constexpr int kMaxBrowserHistory = 16;
constexpr int kMaxHiddenItems = 256;
constexpr int kPlayerLogLines = 32;
constexpr uint64_t kMaxImageBytes = 32ull * 1024ull * 1024ull;
constexpr const char* kDataDir = "ux0:/data/vita-smb-player";
constexpr const char* kDefaultLocalRoot = "ux0:";
constexpr const char* kLocalVideoCopyRoot = "ux0:/video";
constexpr const char* kLocalImageCopyRoot = "ux0:/picture";

#ifndef ALIGN
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif

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
float gTouchStartX = 0.0f;
float gTouchStartY = 0.0f;
float gTouchLastX = 0.0f;
float gTouchLastY = 0.0f;
double gTouchScrubTarget = 0.0;
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

void copyText(char* dst, size_t dstSize, const char* src);
void lockScan();
void unlockScan();
bool isImageFile(const char* name);
void keepSelectedVisible(int selected, int count, int* listTop);
float clampFloat(float value, float lo, float hi);

enum UiLanguage {
    UiZhTw = 0,
    UiEn,
};

struct Translation {
    const char* key;
    const char* zhTw;
    const char* en;
};

UiLanguage gUiLanguage = UiZhTw;

constexpr Translation kTranslations[] = {
    {"app.title", "SMB Player", "SMB Player"},
    {"login.title", "登入", "Login"},
    {"field.server", "伺服器", "Server"},
    {"field.share", "分享名稱", "Share"},
    {"field.path", "路徑", "Path"},
    {"field.user", "使用者", "User"},
    {"field.password", "密碼", "Password"},
    {"field.domain", "網域", "Domain"},
    {"field.local_path", "本機路徑", "Local Path"},
    {"field.empty", "(空白)", "(empty)"},
    {"ime.edit.fmt", "編輯 %s", "Edit %s"},
    {"connect.smb", "連線到 SMB", "Connect to SMB"},
    {"connect.local", "播放本機檔案", "Play local files"},
    {"connect.hidden", "管理隱藏項目", "Manage hidden items"},
    {"connect.smb_button", "連線 SMB", "SMB"},
    {"connect.local_button", "本機", "Local"},
    {"connect.hidden_button", "隱藏項目", "Hidden"},
    {"footer.connect", "方向鍵選擇   ○ 編輯   START 連線   SELECT 本機   L 隱藏   × 退出",
                       "UP/DOWN select   O edit   START connect   SELECT local   L hidden   X exit"},
    {"footer.browser.smb", "方向鍵選擇   ○ 播放/進入   SELECT 隱藏   L 隱藏項目   □ 複製   △ 重新整理   × 返回",
                           "UP/DOWN select   O play   SELECT hide   L hidden   SQUARE copy   TRIANGLE rescan   X exit"},
    {"footer.browser.local", "方向鍵選擇   ○ 播放/進入   SELECT 隱藏   L 隱藏項目   △ 重新整理   × 返回",
                             "UP/DOWN select   O play   SELECT hide   L hidden   TRIANGLE rescan   X exit"},
    {"footer.hidden", "方向鍵選擇   ○ 取消隱藏   × 返回", "UP/DOWN select   O unhide   X back"},
    {"hint.select", "選擇", "Select"},
    {"hint.edit", "編輯", "Edit"},
    {"hint.exit", "退出", "Exit"},
    {"hint.enter", "進入", "Enter"},
    {"hint.play", "播放", "Play"},
    {"hint.view", "檢視", "View"},
    {"hint.hide", "隱藏", "Hide"},
    {"hint.hidden", "隱藏項目", "Hidden"},
    {"hint.copy", "複製", "Copy"},
    {"hint.rescan", "重新整理", "Rescan"},
    {"hint.back", "返回", "Back"},
    {"hint.unhide", "取消隱藏", "Unhide"},
    {"hint.move", "移動", "Move"},
    {"hint.zoom", "縮放", "Zoom"},
    {"hint.reset", "重置", "Reset"},
    {"hint.rotate", "旋轉", "Rotate"},
    {"hint.prev_next", "上/下一張", "Prev/Next"},
    {"hint.hud", "HUD", "HUD"},
    {"hidden.title", "隱藏項目", "Hidden Items"},
    {"hidden.count.fmt", "%d 個隱藏項目", "%d hidden items"},
    {"hidden.hint", "按 ○ 取消隱藏，會立即更新 hidden.json", "Press O to unhide. hidden.json is updated immediately."},
    {"hidden.empty", "目前沒有隱藏項目", "No hidden items"},
    {"local.prefix.fmt", "本機: %s", "Local: %s"},
    {"share.prefix.fmt", "分享: //%s/%s/%s", "Share: //%s/%s/%s"},
    {"runtime.local.fmt", "本機檔案   mpv API: %lu", "Local file   mpv API: %lu"},
    {"runtime.smb.fmt", "libsmb2: %s   mpv API: %lu", "libsmb2: %s   mpv API: %lu"},
    {"runtime.failed", "失敗", "failed"},
    {"browser.empty", "沒有找到資料夾、影片或圖片", "No folders, videos, or images found"},
    {"browser.wait", "請稍候...", "Please wait..."},
    {"scan.idle", "網路尚未啟動", "Network not started"},
    {"scan.network_failed", "Vita 網路初始化失敗", "Vita network initialization failed"},
    {"scan.thread_failed", "建立掃描執行緒失敗", "Failed to create scan thread"},
    {"scan.smb.start", "開始掃描 SMB...", "Starting SMB scan..."},
    {"scan.local.start", "開始掃描本機資料夾...", "Starting local scan..."},
    {"scan.smb.connecting", "正在連線到 SMB 分享...", "Connecting to SMB share..."},
    {"scan.smb.init_failed", "libsmb2 初始化失敗", "Failed to initialize libsmb2 context"},
    {"scan.smb.connect_failed.fmt", "連線失敗: %s", "Connect failed: %s"},
    {"scan.smb.open_failed.fmt", "開啟資料夾 '%s' 失敗: %s", "Open dir '%s' failed: %s"},
    {"scan.local.open_failed.fmt", "開啟本機資料夾失敗: 0x%08x", "Open local folder failed: 0x%08x"},
    {"scan.smb.reading", "正在讀取資料夾...", "Reading directory..."},
    {"scan.local.reading", "正在讀取本機資料夾...", "Reading local folder..."},
    {"scan.smb.loaded.fmt", "已載入 %d 個項目（略過 %d 個）", "Loaded %d entries (%d hidden)"},
    {"scan.local.loaded.fmt", "已載入 %d 個本機項目（略過 %d 個）", "Loaded %d local entries (%d hidden)"},
    {"player.back", "× 返回", "X Back"},
    {"player.rotate.fmt", "△ 旋轉 %d°", "△ Rotate %d°"},
    {"player.loading", "載入中…", "Loading..."},
    {"player.waiting", "等待畫面…", "Waiting for video..."},
    {"player.playing", "播放中", "Playing"},
    {"player.loaded", "檔案已載入", "File loaded"},
    {"player.ended", "播放結束", "Playback ended"},
    {"player.stopped", "播放已停止", "Playback stopped"},
    {"player.paused", "已暫停", "Paused"},
    {"player.resumed", "播放中", "Playing"},
    {"player.stopped_short", "已停止", "Stopped"},
    {"player.ready", "mpv 已就緒", "mpv ready"},
    {"player.loadfile_sent", "已送出播放請求", "loadfile sent"},
    {"player.streaming.fmt", "串流中 %s", "Streaming %s"},
    {"player.dir_unavailable", "請按 ○ 進入資料夾", "Press O to enter folders"},
    {"image.loading", "圖片載入中…", "Loading image..."},
    {"image.too_large.fmt", "圖片檔案太大：%lu MB，上限 32 MB。", "Image too large: %lu MB. Limit is 32 MB."},
    {"image.open_failed", "圖片開啟失敗", "Failed to open image"},
    {"image.read_failed", "圖片讀取失敗", "Failed to read image"},
    {"image.decode_failed", "圖片格式無法解碼", "Failed to decode image"},
    {"error.resolution.title", "解析度不支援", "Resolution unsupported"},
    {"error.resolution.reason.fmt", "目前影片解析度：%dx%d，超過 1080p 播放上限。請轉成 1920x1080 或 1080x1920 以下再播放。",
                                    "Current video resolution: %dx%d. It exceeds the 1080p playback limit. Convert to 1920x1080 or 1080x1920 or below."},
    {"error.resolution.hint", "直式影片請維持短邊 1080、長邊 1920 以下。",
                              "For vertical video, keep the short side <= 1080 and long side <= 1920."},
    {"error.convert.hint", "建議轉成 H.264 8-bit + AAC stereo MP4 後播放。",
                           "Convert to H.264 8-bit + AAC stereo MP4 before playback."},
    {"error.format.title", "格式不支援", "Format unsupported"},
    {"error.codec.title", "編碼不支援", "Codec unsupported"},
    {"error.container.title", "容器不支援", "Container unsupported"},
    {"error.no_stream", "目前檔案沒有可播放的音訊或影片軌。", "This file has no playable audio or video track."},
    {"error.codec.reason", "此檔案使用目前 Vita 版本無法解碼的格式。", "This file uses a format this Vita build cannot decode."},
    {"error.container.reason", "目前檔案容器無法辨識。", "The file container could not be recognized."},
    {"error.unsupported.video_audio.fmt", "目前影片格式：%s，音訊格式：%s，此 Vita 版本無法播放。",
                                         "Current video codec: %s, audio codec: %s. This Vita build cannot play it."},
    {"error.unsupported.video.fmt", "目前影片格式：%s，此 Vita 版本無法播放。",
                                   "Current video codec: %s. This Vita build cannot play it."},
    {"error.unsupported.audio.fmt", "目前音訊格式：%s，此 Vita 版本無法播放。",
                                   "Current audio codec: %s. This Vita build cannot play it."},
    {"copy.connecting", "準備複製連線...", "Connecting for copy..."},
    {"copy.preparing", "準備複製...", "Preparing copy..."},
    {"copy.running", "複製中...", "Copying..."},
    {"copy.complete", "複製完成", "Copy complete"},
    {"copy.init_failed", "複製失敗：libsmb2 初始化失敗", "Copy failed: libsmb2 init failed"},
    {"copy.oom", "複製失敗：記憶體不足", "Copy failed: out of memory"},
    {"copy.thread_failed", "複製失敗：無法建立執行緒", "Copy failed: thread create failed"},
    {"copy.connect_failed.fmt", "複製連線失敗：%s", "Copy connect failed: %s"},
    {"copy.open_failed.fmt", "開啟來源檔案失敗：%s", "Copy open failed: %s"},
    {"copy.create_failed.fmt", "建立本機檔案失敗：0x%08x", "Copy create failed: 0x%08x"},
    {"copy.read_failed.fmt", "讀取來源檔案失敗：%s", "Copy read failed: %s"},
    {"copy.write_failed.fmt", "寫入本機檔案失敗：0x%08x", "Copy write failed: 0x%08x"},
    {"copy.busy.fmt", "複製中 %d%%  %s", "Copying %d%%  %s"},
    {"copy.error.fmt", "複製失敗：%s", "Copy failed: %s"},
    {"copy.done.fmt", "複製完成：%s", "Copy complete: %s"},
};

constexpr int kTranslationCount = static_cast<int>(sizeof(kTranslations) / sizeof(kTranslations[0]));
char gTranslationOverrides[kTranslationCount][256] = {};

const char* t(const char* key) {
    for (int i = 0; i < kTranslationCount; ++i) {
        const Translation& entry = kTranslations[i];
        if (std::strcmp(entry.key, key) == 0) {
            if (gTranslationOverrides[i][0]) return gTranslationOverrides[i];
            return gUiLanguage == UiEn ? entry.en : entry.zhTw;
        }
    }
    return key;
}

void initUiLanguageFromSystem() {
    int lang = SCE_SYSTEM_PARAM_LANG_CHINESE_T;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_LANG, &lang) < 0) return;
    if (lang == SCE_SYSTEM_PARAM_LANG_ENGLISH_US ||
        lang == SCE_SYSTEM_PARAM_LANG_ENGLISH_GB) {
        gUiLanguage = UiEn;
    } else {
        gUiLanguage = UiZhTw;
    }
}

void applyTranslationOverride(const char* key, const char* value) {
    if (!key || !value) return;
    for (int i = 0; i < kTranslationCount; ++i) {
        if (std::strcmp(kTranslations[i].key, key) == 0) {
            copyText(gTranslationOverrides[i], sizeof(gTranslationOverrides[i]), value);
            return;
        }
    }
}

const char* optionalText(const char* value) {
    return value && value[0] ? value : nullptr;
}

const char* connectFieldLabel(int field) {
    switch (field) {
    case ConnectServer: return t("field.server");
    case ConnectShare: return t("field.share");
    case ConnectPath: return t("field.path");
    case ConnectUser: return t("field.user");
    case ConnectPassword: return t("field.password");
    case ConnectDomain: return t("field.domain");
    case ConnectLocalPath: return t("field.local_path");
    default: return "";
    }
}

char* connectFieldValue(int field) {
    switch (field) {
    case ConnectServer: return gConn.server;
    case ConnectShare: return gConn.share;
    case ConnectPath: return gConn.path;
    case ConnectUser: return gConn.user;
    case ConnectPassword: return gConn.password;
    case ConnectDomain: return gConn.domain;
    case ConnectLocalPath: return gConn.localPath;
    default: return nullptr;
    }
}

size_t connectFieldSize(int field) {
    switch (field) {
    case ConnectServer: return sizeof(gConn.server);
    case ConnectShare: return sizeof(gConn.share);
    case ConnectPath: return sizeof(gConn.path);
    case ConnectUser: return sizeof(gConn.user);
    case ConnectPassword: return sizeof(gConn.password);
    case ConnectDomain: return sizeof(gConn.domain);
    case ConnectLocalPath: return sizeof(gConn.localPath);
    default: return 0;
    }
}

void utf8ToUtf16(const char* src, SceWChar16* dst, size_t dstCount) {
    if (!dst || dstCount == 0) return;
    size_t out = 0;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src ? src : "");
    while (*s && out + 1 < dstCount) {
        unsigned int cp = 0;
        if (*s < 0x80) {
            cp = *s++;
        } else if ((*s & 0xE0) == 0xC0 && s[1]) {
            cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            ++s;
            continue;
        }
        dst[out++] = static_cast<SceWChar16>(cp <= 0xFFFF ? cp : '?');
    }
    dst[out] = 0;
}

void utf16ToUtf8(const SceWChar16* src, char* dst, size_t dstSize) {
    if (!dst || dstSize == 0) return;
    size_t out = 0;
    for (; src && *src && out + 1 < dstSize; ++src) {
        const unsigned int cp = *src;
        if (cp < 0x80) {
            dst[out++] = static_cast<char>(cp);
        } else if (cp < 0x800 && out + 2 < dstSize) {
            dst[out++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (out + 3 < dstSize) {
            dst[out++] = static_cast<char>(0xE0 | (cp >> 12));
            dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            break;
        }
    }
    dst[out] = '\0';
}

void initConnectionDefaults() {
    copyText(gConn.server, sizeof(gConn.server), VITA_SMB_SERVER);
    copyText(gConn.share, sizeof(gConn.share), VITA_SMB_SHARE);
    copyText(gConn.path, sizeof(gConn.path), VITA_SMB_PATH);
    copyText(gConn.user, sizeof(gConn.user), VITA_SMB_USER);
    copyText(gConn.password, sizeof(gConn.password), VITA_SMB_PASSWORD);
    copyText(gConn.domain, sizeof(gConn.domain), VITA_SMB_DOMAIN);
    copyText(gConn.localPath, sizeof(gConn.localPath), kDefaultLocalRoot);
}

void trimLine(char* line) {
    if (!line) return;
    for (size_t i = 0; line[i]; ++i) {
        if (line[i] == '\r' || line[i] == '\n') {
            line[i] = '\0';
            break;
        }
    }
}

void applyConfigLine(const char* key, const char* value) {
    if (std::strcmp(key, "server") == 0) copyText(gConn.server, sizeof(gConn.server), value);
    else if (std::strcmp(key, "share") == 0) copyText(gConn.share, sizeof(gConn.share), value);
    else if (std::strcmp(key, "path") == 0) copyText(gConn.path, sizeof(gConn.path), value);
    else if (std::strcmp(key, "user") == 0) copyText(gConn.user, sizeof(gConn.user), value);
    else if (std::strcmp(key, "password") == 0) copyText(gConn.password, sizeof(gConn.password), value);
    else if (std::strcmp(key, "domain") == 0) copyText(gConn.domain, sizeof(gConn.domain), value);
    else if (std::strcmp(key, "local_path") == 0) copyText(gConn.localPath, sizeof(gConn.localPath), value);
}

void loadConnectionConfig() {
    FILE* fp = std::fopen("ux0:/data/vita-smb-player/connection.txt", "r");
    if (!fp) return;
    char line[384];
    while (std::fgets(line, sizeof(line), fp)) {
        trimLine(line);
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        applyConfigLine(line, eq + 1);
    }
    std::fclose(fp);
    if (!gConn.localPath[0]) copyText(gConn.localPath, sizeof(gConn.localPath), kDefaultLocalRoot);
}

void saveConnectionConfig() {
    FILE* fp = std::fopen("ux0:/data/vita-smb-player/connection.txt", "w");
    if (!fp) return;
    std::fprintf(fp, "server=%s\n", gConn.server);
    std::fprintf(fp, "share=%s\n", gConn.share);
    std::fprintf(fp, "path=%s\n", gConn.path);
    std::fprintf(fp, "user=%s\n", gConn.user);
    std::fprintf(fp, "password=%s\n", gConn.password);
    std::fprintf(fp, "domain=%s\n", gConn.domain);
    std::fprintf(fp, "local_path=%s\n", gConn.localPath);
    std::fclose(fp);
}

void writeJsonString(FILE* fp, const char* text) {
    std::fputc('"', fp);
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : ""); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            std::fputc('\\', fp);
            std::fputc(*p, fp);
        } else if (*p == '\n') {
            std::fputs("\\n", fp);
        } else if (*p == '\r') {
            std::fputs("\\r", fp);
        } else if (*p == '\t') {
            std::fputs("\\t", fp);
        } else if (*p < 0x20) {
            std::fputc('?', fp);
        } else {
            std::fputc(*p, fp);
        }
    }
    std::fputc('"', fp);
}

const char* skipJsonSpace(const char* p) {
    while (p && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    return p;
}

bool parseJsonString(const char** cursor, char* out, size_t outSize) {
    const char* p = skipJsonSpace(*cursor);
    if (!p || *p != '"') return false;
    ++p;

    size_t outPos = 0;
    while (*p && *p != '"') {
        unsigned char ch = static_cast<unsigned char>(*p++);
        if (ch == '\\') {
            const char esc = *p++;
            if (esc == 'n') ch = '\n';
            else if (esc == 'r') ch = '\r';
            else if (esc == 't') ch = '\t';
            else if (esc == '"' || esc == '\\' || esc == '/') ch = static_cast<unsigned char>(esc);
            else ch = '?';
        }
        if (outPos + 1 < outSize) out[outPos++] = static_cast<char>(ch);
    }
    if (*p != '"') return false;
    if (outSize > 0) out[outPos] = '\0';
    *cursor = p + 1;
    return true;
}

bool consumeJsonChar(const char** cursor, char expected) {
    const char* p = skipJsonSpace(*cursor);
    if (!p || *p != expected) return false;
    *cursor = p + 1;
    return true;
}

void skipJsonValue(const char** cursor);

void skipJsonObject(const char** cursor) {
    if (!consumeJsonChar(cursor, '{')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        char key[192];
        if (!parseJsonString(cursor, key, sizeof(key))) return;
        if (!consumeJsonChar(cursor, ':')) return;
        skipJsonValue(cursor);
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void skipJsonArray(const char** cursor) {
    if (!consumeJsonChar(cursor, '[')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == ']') {
            *cursor = p + 1;
            return;
        }
        skipJsonValue(cursor);
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == ']') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void skipJsonValue(const char** cursor) {
    const char* p = skipJsonSpace(*cursor);
    if (!p) return;
    if (*p == '{') {
        skipJsonObject(cursor);
    } else if (*p == '[') {
        skipJsonArray(cursor);
    } else if (*p == '"') {
        char tmp[8];
        parseJsonString(cursor, tmp, sizeof(tmp));
    } else {
        while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
        *cursor = p;
    }
}

bool parseTranslationObject(const char** cursor, const char* prefix, int depth) {
    if (depth > 8 || !consumeJsonChar(cursor, '{')) return false;

    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (!p || *p == '\0') return false;
        if (*p == '}') {
            *cursor = p + 1;
            return true;
        }

        char key[96];
        if (!parseJsonString(cursor, key, sizeof(key))) return false;
        if (!consumeJsonChar(cursor, ':')) return false;

        char fullKey[160];
        if (prefix && prefix[0]) {
            std::snprintf(fullKey, sizeof(fullKey), "%s.%s", prefix, key);
        } else {
            copyText(fullKey, sizeof(fullKey), key);
        }

        p = skipJsonSpace(*cursor);
        if (p && *p == '"') {
            char value[256];
            if (!parseJsonString(cursor, value, sizeof(value))) return false;
            applyTranslationOverride(fullKey, value);
        } else if (p && *p == '{') {
            if (!parseTranslationObject(cursor, fullKey, depth + 1)) return false;
        } else {
            skipJsonValue(cursor);
        }

        p = skipJsonSpace(*cursor);
        if (p && *p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (p && *p == '}') {
            *cursor = p + 1;
            return true;
        }
        return false;
    }
}

bool loadTranslationJson(const char* path) {
    FILE* fp = std::fopen(path, "r");
    if (!fp) return false;

    if (std::fseek(fp, 0, SEEK_END) != 0) {
        std::fclose(fp);
        return false;
    }
    const long size = std::ftell(fp);
    if (size <= 0 || size > 64 * 1024) {
        std::fclose(fp);
        return false;
    }
    std::rewind(fp);

    char* data = static_cast<char*>(std::malloc(static_cast<size_t>(size) + 1));
    if (!data) {
        std::fclose(fp);
        return false;
    }
    const size_t read = std::fread(data, 1, static_cast<size_t>(size), fp);
    std::fclose(fp);
    data[read] = '\0';

    const char* cursor = data;
    const bool parsed = parseTranslationObject(&cursor, "", 0);

    std::free(data);
    return parsed;
}

void loadUiTranslationsFromResource() {
    for (int i = 0; i < kTranslationCount; ++i) {
        gTranslationOverrides[i][0] = '\0';
    }

    char path[128];
    std::snprintf(path, sizeof(path), "app0:/i18n/%s/vita_smb_player.json",
                  gUiLanguage == UiEn ? "en-US" : "zh-Hant");
    if (!loadTranslationJson(path) && gUiLanguage != UiZhTw) {
        loadTranslationJson("app0:/i18n/zh-Hant/vita_smb_player.json");
    }
}

bool appendHiddenItemLoaded(int source, const char* server, const char* share, const char* path, const char* name) {
    if (!name || !name[0] || gHiddenItemCount >= kMaxHiddenItems) return false;
    HiddenItem& item = gHiddenItems[gHiddenItemCount++];
    item.source = source;
    copyText(item.server, sizeof(item.server), server ? server : "");
    copyText(item.share, sizeof(item.share), share ? share : "");
    copyText(item.path, sizeof(item.path), path ? path : "");
    copyText(item.name, sizeof(item.name), name);
    return true;
}

void parseHiddenNameArray(const char** cursor, int source, const char* server, const char* share, const char* path) {
    if (!consumeJsonChar(cursor, '[')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == ']') {
            *cursor = p + 1;
            return;
        }
        char name[192];
        if (parseJsonString(cursor, name, sizeof(name))) {
            appendHiddenItemLoaded(source, server, share, path, name);
        } else {
            skipJsonValue(cursor);
        }
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == ']') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void parseHiddenPathMap(const char** cursor, int source, const char* server, const char* share) {
    if (!consumeJsonChar(cursor, '{')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        char path[256];
        if (!parseJsonString(cursor, path, sizeof(path))) return;
        if (!consumeJsonChar(cursor, ':')) return;
        parseHiddenNameArray(cursor, source, server, share, path);
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void parseHiddenShareMap(const char** cursor, const char* server) {
    if (!consumeJsonChar(cursor, '{')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        char share[64];
        if (!parseJsonString(cursor, share, sizeof(share))) return;
        if (!consumeJsonChar(cursor, ':')) return;
        parseHiddenPathMap(cursor, SourceSmb, server, share);
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void parseHiddenSmbMap(const char** cursor) {
    if (!consumeJsonChar(cursor, '{')) return;
    while (true) {
        const char* p = skipJsonSpace(*cursor);
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        char server[128];
        if (!parseJsonString(cursor, server, sizeof(server))) return;
        if (!consumeJsonChar(cursor, ':')) return;
        parseHiddenShareMap(cursor, server);
        p = skipJsonSpace(*cursor);
        if (*p == ',') {
            *cursor = p + 1;
            continue;
        }
        if (*p == '}') {
            *cursor = p + 1;
            return;
        }
        return;
    }
}

void parseHiddenLocalMap(const char** cursor) {
    parseHiddenPathMap(cursor, SourceLocal, "", "");
}

void loadHiddenItems() {
    gHiddenItemCount = 0;
    FILE* fp = std::fopen("ux0:/data/vita-smb-player/hidden.json", "r");
    if (!fp) return;

    std::fseek(fp, 0, SEEK_END);
    long len = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (len <= 0 || len > 128 * 1024) {
        std::fclose(fp);
        return;
    }

    char* json = static_cast<char*>(std::malloc(static_cast<size_t>(len) + 1));
    if (!json) {
        std::fclose(fp);
        return;
    }
    const size_t read = std::fread(json, 1, static_cast<size_t>(len), fp);
    std::fclose(fp);
    json[read] = '\0';

    const char* cursor = json;
    if (consumeJsonChar(&cursor, '{')) {
        while (true) {
            const char* p = skipJsonSpace(cursor);
            if (*p == '}') break;
            char key[64];
            if (!parseJsonString(&cursor, key, sizeof(key))) break;
            if (!consumeJsonChar(&cursor, ':')) break;
            if (std::strcmp(key, "smb") == 0) {
                parseHiddenSmbMap(&cursor);
            } else if (std::strcmp(key, "local") == 0) {
                parseHiddenLocalMap(&cursor);
            } else {
                skipJsonValue(&cursor);
            }
            p = skipJsonSpace(cursor);
            if (*p == ',') {
                cursor = p + 1;
                continue;
            }
            break;
        }
    }
    std::free(json);
}

bool hiddenItemMatchesSmbGroup(const HiddenItem& item, const char* server, const char* share, const char* path) {
    return item.source == SourceSmb &&
           std::strcmp(item.server, server) == 0 &&
           std::strcmp(item.share, share) == 0 &&
           std::strcmp(item.path, path) == 0;
}

bool hiddenItemMatchesLocalGroup(const HiddenItem& item, const char* path) {
    return item.source == SourceLocal && std::strcmp(item.path, path) == 0;
}

bool smbServerWrittenBefore(int index) {
    for (int i = 0; i < index; ++i) {
        if (gHiddenItems[i].source == SourceSmb &&
            std::strcmp(gHiddenItems[i].server, gHiddenItems[index].server) == 0) {
            return true;
        }
    }
    return false;
}

bool smbShareWrittenBefore(int index) {
    for (int i = 0; i < index; ++i) {
        if (gHiddenItems[i].source == SourceSmb &&
            std::strcmp(gHiddenItems[i].server, gHiddenItems[index].server) == 0 &&
            std::strcmp(gHiddenItems[i].share, gHiddenItems[index].share) == 0) {
            return true;
        }
    }
    return false;
}

bool smbPathWrittenBefore(int index) {
    for (int i = 0; i < index; ++i) {
        if (hiddenItemMatchesSmbGroup(gHiddenItems[i],
                                      gHiddenItems[index].server,
                                      gHiddenItems[index].share,
                                      gHiddenItems[index].path)) {
            return true;
        }
    }
    return false;
}

bool localPathWrittenBefore(int index) {
    for (int i = 0; i < index; ++i) {
        if (hiddenItemMatchesLocalGroup(gHiddenItems[i], gHiddenItems[index].path)) {
            return true;
        }
    }
    return false;
}

void writeHiddenNameArray(FILE* fp, int source, const char* server, const char* share, const char* path) {
    std::fputs("[", fp);
    bool first = true;
    for (int i = 0; i < gHiddenItemCount; ++i) {
        const HiddenItem& item = gHiddenItems[i];
        const bool match = source == SourceSmb
            ? hiddenItemMatchesSmbGroup(item, server, share, path)
            : hiddenItemMatchesLocalGroup(item, path);
        if (!match) continue;
        if (!first) std::fputs(", ", fp);
        writeJsonString(fp, item.name);
        first = false;
    }
    std::fputs("]", fp);
}

void saveHiddenItems() {
    FILE* fp = std::fopen("ux0:/data/vita-smb-player/hidden.json", "w");
    if (!fp) return;

    std::fputs("{\n  \"version\": 1,\n  \"smb\": {\n", fp);
    bool firstServer = true;
    for (int si = 0; si < gHiddenItemCount; ++si) {
        if (gHiddenItems[si].source != SourceSmb || smbServerWrittenBefore(si)) continue;
        if (!firstServer) std::fputs(",\n", fp);
        std::fputs("    ", fp);
        writeJsonString(fp, gHiddenItems[si].server);
        std::fputs(": {\n", fp);

        bool firstShare = true;
        for (int hi = 0; hi < gHiddenItemCount; ++hi) {
            if (gHiddenItems[hi].source != SourceSmb ||
                std::strcmp(gHiddenItems[hi].server, gHiddenItems[si].server) != 0 ||
                smbShareWrittenBefore(hi)) {
                continue;
            }
            if (!firstShare) std::fputs(",\n", fp);
            std::fputs("      ", fp);
            writeJsonString(fp, gHiddenItems[hi].share);
            std::fputs(": {\n", fp);

            bool firstPath = true;
            for (int pi = 0; pi < gHiddenItemCount; ++pi) {
                if (gHiddenItems[pi].source != SourceSmb ||
                    std::strcmp(gHiddenItems[pi].server, gHiddenItems[si].server) != 0 ||
                    std::strcmp(gHiddenItems[pi].share, gHiddenItems[hi].share) != 0 ||
                    smbPathWrittenBefore(pi)) {
                    continue;
                }
                if (!firstPath) std::fputs(",\n", fp);
                std::fputs("        ", fp);
                writeJsonString(fp, gHiddenItems[pi].path);
                std::fputs(": ", fp);
                writeHiddenNameArray(fp, SourceSmb, gHiddenItems[si].server, gHiddenItems[hi].share, gHiddenItems[pi].path);
                firstPath = false;
            }
            std::fputs("\n      }", fp);
            firstShare = false;
        }
        std::fputs("\n    }", fp);
        firstServer = false;
    }
    std::fputs("\n  },\n  \"local\": {\n", fp);

    bool firstLocalPath = true;
    for (int i = 0; i < gHiddenItemCount; ++i) {
        if (gHiddenItems[i].source != SourceLocal || localPathWrittenBefore(i)) continue;
        if (!firstLocalPath) std::fputs(",\n", fp);
        std::fputs("    ", fp);
        writeJsonString(fp, gHiddenItems[i].path);
        std::fputs(": ", fp);
        writeHiddenNameArray(fp, SourceLocal, "", "", gHiddenItems[i].path);
        firstLocalPath = false;
    }
    std::fputs("\n  }\n}\n", fp);
    std::fclose(fp);
}

bool isUserHiddenItem(int source, const char* server, const char* share, const char* path, const char* name) {
    if (!server) server = "";
    if (!share) share = "";
    if (!path) path = "";
    if (!name) name = "";
    for (int i = 0; i < gHiddenItemCount; ++i) {
        const HiddenItem& item = gHiddenItems[i];
        if (item.source == source &&
            std::strcmp(item.server, server) == 0 &&
            std::strcmp(item.share, share) == 0 &&
            std::strcmp(item.path, path) == 0 &&
            std::strcmp(item.name, name) == 0) {
            return true;
        }
    }
    return false;
}

void addHiddenItem(int source, const char* server, const char* share, const char* path, const char* name) {
    if (!name || !name[0]) return;
    if (!server) server = "";
    if (!share) share = "";
    if (!path) path = "";
    if (isUserHiddenItem(source, server, share, path, name)) return;

    if (gHiddenItemCount >= kMaxHiddenItems) {
        for (int i = 1; i < kMaxHiddenItems; ++i) {
            gHiddenItems[i - 1] = gHiddenItems[i];
        }
        gHiddenItemCount = kMaxHiddenItems - 1;
    }

    HiddenItem& item = gHiddenItems[gHiddenItemCount++];
    item.source = source;
    copyText(item.server, sizeof(item.server), server);
    copyText(item.share, sizeof(item.share), share);
    copyText(item.path, sizeof(item.path), path);
    copyText(item.name, sizeof(item.name), name);
    saveHiddenItems();
}

void removeHiddenItemAt(int index) {
    if (index < 0 || index >= gHiddenItemCount) return;
    for (int i = index + 1; i < gHiddenItemCount; ++i) {
        gHiddenItems[i - 1] = gHiddenItems[i];
    }
    --gHiddenItemCount;
    saveHiddenItems();
}

void openImeForField(int field) {
    char* value = connectFieldValue(field);
    const size_t valueSize = connectFieldSize(field);
    if (!value || valueSize == 0 || gImeOpen) return;

    std::memset(gImeBuffer, 0, sizeof(gImeBuffer));
    utf8ToUtf16(value, gImeBuffer, sizeof(gImeBuffer) / sizeof(gImeBuffer[0]));

    std::memset(gImeTitle, 0, sizeof(gImeTitle));
    char titleText[96];
    std::snprintf(titleText, sizeof(titleText), t("ime.edit.fmt"), connectFieldLabel(field));
    utf8ToUtf16(titleText, gImeTitle, sizeof(gImeTitle) / sizeof(gImeTitle[0]));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_TRADITIONAL_CHINESE
                             | SCE_IME_LANGUAGE_SIMPLIFIED_CHINESE
                             | SCE_IME_LANGUAGE_JAPANESE
                             | SCE_IME_LANGUAGE_ENGLISH;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = field == ConnectPassword
        ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
        : SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = gImeTitle;
    param.maxTextLength = static_cast<SceUInt32>(valueSize > 1 ? valueSize - 1 : 0);
    param.inputTextBuffer = gImeBuffer;
    param.initialText = gImeBuffer;

    if (sceImeDialogInit(&param) >= 0) {
        gImeOpen = true;
        gImeField = field;
    }
}

void updateImeDialog() {
    if (!gImeOpen) return;
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;

    SceImeDialogResult result = {};
    sceImeDialogGetResult(&result);
    sceImeDialogTerm();

    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        char* value = connectFieldValue(gImeField);
        const size_t valueSize = connectFieldSize(gImeField);
        if (value && valueSize > 0) {
            utf16ToUtf8(gImeBuffer, value, valueSize);
            if (gImeField == ConnectLocalPath && !value[0]) {
                copyText(value, valueSize, kDefaultLocalRoot);
            }
            saveConnectionConfig();
        }
    }

    gImeOpen = false;
    gImeField = -1;
}

const char* smbPathForDisplay() {
    const char* path = optionalText(gConn.path);
    return path ? path : "";
}

const char* normalizeSmbPath() {
    const char* path = optionalText(gConn.path);
    if (!path) return "";
    while (*path == '/') ++path;
    return path;
}

void initCurrentPath() {
    const char* initial = normalizeSmbPath();
    copyText(gCurrentPath, sizeof(gCurrentPath), initial);
    gBrowserHistoryCount = 0;
}

void getConfiguredLocalRoot(char* out, size_t outSize) {
    copyText(out, outSize, optionalText(gConn.localPath) ? gConn.localPath : kDefaultLocalRoot);
    size_t len = std::strlen(out);
    while (len > 3 && out[len - 1] == '/') {
        out[len - 1] = '\0';
        --len;
    }
    if (!out[0]) copyText(out, outSize, kDefaultLocalRoot);
}

void initLocalPath() {
    getConfiguredLocalRoot(gCurrentPath, sizeof(gCurrentPath));
    gBrowserHistoryCount = 0;
}

void getCurrentPath(char* out, size_t outSize) {
    lockScan();
    copyText(out, outSize, gCurrentPath);
    unlockScan();
}

bool currentPathIsRoot() {
    lockScan();
    bool root = false;
    if (gBrowserSource == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        root = std::strcmp(gCurrentPath, localRoot) == 0;
    } else {
        root = gCurrentPath[0] == '\0';
    }
    unlockScan();
    return root;
}

void pushBrowserFocus(int selected, int listTop) {
    if (gBrowserHistoryCount >= kMaxBrowserHistory) {
        for (int i = 1; i < kMaxBrowserHistory; ++i) {
            gBrowserHistory[i - 1] = gBrowserHistory[i];
        }
        gBrowserHistoryCount = kMaxBrowserHistory - 1;
    }
    BrowserHistoryEntry& entry = gBrowserHistory[gBrowserHistoryCount++];
    copyText(entry.path, sizeof(entry.path), gCurrentPath);
    entry.selected = selected;
    entry.listTop = listTop;
}

bool popBrowserFocus(int* selected, int* listTop) {
    if (gBrowserHistoryCount <= 0) return false;
    const BrowserHistoryEntry& entry = gBrowserHistory[--gBrowserHistoryCount];
    if (std::strcmp(entry.path, gCurrentPath) != 0) {
        return false;
    }
    if (selected) *selected = entry.selected;
    if (listTop) *listTop = entry.listTop;
    return true;
}

void enterDirectory(const char* name, int selected, int listTop) {
    if (!name || !name[0]) return;
    lockScan();
    pushBrowserFocus(selected, listTop);
    if (gCurrentPath[0]) {
        const size_t len = std::strlen(gCurrentPath);
        std::snprintf(gCurrentPath + len, sizeof(gCurrentPath) - len, "/%s", name);
    } else {
        copyText(gCurrentPath, sizeof(gCurrentPath), name);
    }
    unlockScan();
}

bool goParentDirectory() {
    lockScan();
    char localRoot[256];
    getConfiguredLocalRoot(localRoot, sizeof(localRoot));
    if ((gBrowserSource == SourceLocal && std::strcmp(gCurrentPath, localRoot) == 0) ||
        (gBrowserSource == SourceSmb && !gCurrentPath[0])) {
        unlockScan();
        return false;
    }
    char* slash = std::strrchr(gCurrentPath, '/');
    if (slash) *slash = '\0';
    else gCurrentPath[0] = '\0';
    if (gBrowserSource == SourceLocal &&
        std::strncmp(gCurrentPath, localRoot, std::strlen(localRoot)) != 0) {
        copyText(gCurrentPath, sizeof(gCurrentPath), localRoot);
    }
    unlockScan();
    return true;
}

void buildSmbFilePath(const char* fileName, char* out, size_t outSize) {
    char base[256];
    getCurrentPath(base, sizeof(base));
    if (base[0] == '\0') {
        std::snprintf(out, outSize, "%s", fileName);
        return;
    }
    std::snprintf(out, outSize, "%s/%s", base, fileName);
}

void buildLocalFilePath(const char* fileName, char* out, size_t outSize) {
    char base[256];
    getCurrentPath(base, sizeof(base));
    if (base[0] == '\0') {
        std::snprintf(out, outSize, "%s", fileName);
        return;
    }
    std::snprintf(out, outSize, "%s/%s", base, fileName);
}

float fitImageScale(int width, int height, int rotationDegrees) {
    if (width <= 0 || height <= 0) return 1.0f;
    const bool rotated = rotationDegrees == 90 || rotationDegrees == 270;
    const float viewW = rotated ? static_cast<float>(height) : static_cast<float>(width);
    const float viewH = rotated ? static_cast<float>(width) : static_cast<float>(height);
    const float scaleX = kWidth / viewW;
    const float scaleY = kHeight / viewH;
    const float scale = scaleX < scaleY ? scaleX : scaleY;
    return scale < 1.0f ? scale : 1.0f;
}

void clampImageViewLocked() {
    if (!gImage.loaded || gImage.width <= 0 || gImage.height <= 0) {
        gImage.offsetX = 0.0f;
        gImage.offsetY = 0.0f;
        return;
    }

    const bool rotated = gImage.rotationDegrees == 90 || gImage.rotationDegrees == 270;
    const float displayedW = static_cast<float>(rotated ? gImage.height : gImage.width) * gImage.zoom;
    const float displayedH = static_cast<float>(rotated ? gImage.width : gImage.height) * gImage.zoom;
    const float maxX = displayedW > kWidth ? (displayedW - kWidth) * 0.5f : 0.0f;
    const float maxY = displayedH > kHeight ? (displayedH - kHeight) * 0.5f : 0.0f;
    gImage.offsetX = clampFloat(gImage.offsetX, -maxX, maxX);
    gImage.offsetY = clampFloat(gImage.offsetY, -maxY, maxY);
}

void resetImageView() {
    lockScan();
    gImage.zoom = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    clampImageViewLocked();
    unlockScan();
}

void closeImage(NVGcontext* vg) {
    lockScan();
    const int image = gImage.nvgImage;
    gImage.nvgImage = 0;
    gImage.loaded = 0;
    gImage.error = 0;
    gImage.width = 0;
    gImage.height = 0;
    gImage.rotationDegrees = 0;
    gImage.hudVisible = 1;
    gImage.zoom = 1.0f;
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    gImage.fileName[0] = '\0';
    gImage.message[0] = '\0';
    unlockScan();
    if (vg && image) nvgDeleteImage(vg, image);
}

void setImageError(const char* fileName, const char* message) {
    lockScan();
    gImage.loaded = 0;
    gImage.error = 1;
    copyText(gImage.fileName, sizeof(gImage.fileName), fileName);
    copyText(gImage.message, sizeof(gImage.message), message);
    unlockScan();
}

bool readLocalFileToMemory(const char* path, uint64_t expectedSize, unsigned char** outData, int* outSize) {
    if (!path || !outData || !outSize || expectedSize == 0 || expectedSize > kMaxImageBytes) return false;
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return false;

    unsigned char* data = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(expectedSize)));
    if (!data) {
        sceIoClose(fd);
        return false;
    }

    uint64_t total = 0;
    while (total < expectedSize) {
        const uint64_t remaining = expectedSize - total;
        const SceSize chunk = remaining > 256 * 1024 ? 256 * 1024 : static_cast<SceSize>(remaining);
        const SceSSize rc = sceIoRead(fd, data + total, chunk);
        if (rc <= 0) {
            std::free(data);
            sceIoClose(fd);
            return false;
        }
        total += static_cast<uint64_t>(rc);
    }

    sceIoClose(fd);
    *outData = data;
    *outSize = static_cast<int>(total);
    return true;
}

bool readSmbFileToMemory(const char* path, uint64_t expectedSize, unsigned char** outData, int* outSize) {
    if (!path || !outData || !outSize || expectedSize == 0 || expectedSize > kMaxImageBytes) return false;

    struct smb2_context* smb = smb2_init_context();
    if (!smb) return false;
    smb2_set_timeout(smb, 8);
    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (optionalText(gConn.domain)) smb2_set_domain(smb, gConn.domain);
    if (optionalText(gConn.password)) smb2_set_password(smb, gConn.password);

    if (smb2_connect_share(smb, gConn.server, gConn.share, optionalText(gConn.user)) < 0) {
        smb2_destroy_context(smb);
        return false;
    }

    struct smb2fh* fh = smb2_open(smb, path, O_RDONLY);
    if (!fh) {
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    uint64_t size = expectedSize;
    struct smb2_stat_64 st = {};
    if (smb2_fstat(smb, fh, &st) == 0 && st.smb2_size > 0) {
        size = static_cast<uint64_t>(st.smb2_size);
    }
    if (size == 0 || size > kMaxImageBytes) {
        smb2_close(smb, fh);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    unsigned char* data = static_cast<unsigned char*>(std::malloc(static_cast<size_t>(size)));
    if (!data) {
        smb2_close(smb, fh);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        return false;
    }

    uint64_t total = 0;
    while (total < size) {
        const uint64_t remaining = size - total;
        const uint32_t chunk = remaining > 256 * 1024 ? 256 * 1024 : static_cast<uint32_t>(remaining);
        const int rc = smb2_read(smb, fh, data + total, chunk);
        if (rc <= 0) {
            std::free(data);
            smb2_close(smb, fh);
            smb2_disconnect_share(smb);
            smb2_destroy_context(smb);
            return false;
        }
        total += static_cast<uint64_t>(rc);
    }

    smb2_close(smb, fh);
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);
    *outData = data;
    *outSize = static_cast<int>(total);
    return true;
}

bool openImageEntry(const SmbEntry& entry, int source, NVGcontext* vg, bool preserveHud = false) {
    if (entry.directory || !isImageFile(entry.name)) return false;
    int hudVisible = 1;
    if (preserveHud) {
        lockScan();
        hudVisible = gImage.hudVisible;
        unlockScan();
    }

    if (entry.size > kMaxImageBytes) {
        char msg[160];
        const unsigned long mb = static_cast<unsigned long>((entry.size + 1024 * 1024 - 1) / (1024 * 1024));
        std::snprintf(msg, sizeof(msg), t("image.too_large.fmt"), mb);
        setImageError(entry.name, msg);
        return true;
    }

    unsigned char* data = nullptr;
    int dataSize = 0;
    char path[320];
    if (source == SourceLocal) {
        buildLocalFilePath(entry.name, path, sizeof(path));
        if (!readLocalFileToMemory(path, entry.size, &data, &dataSize)) {
            setImageError(entry.name, t("image.read_failed"));
            return true;
        }
    } else {
        buildSmbFilePath(entry.name, path, sizeof(path));
        if (!readSmbFileToMemory(path, entry.size, &data, &dataSize)) {
            setImageError(entry.name, t("image.read_failed"));
            return true;
        }
    }

    const int image = nvgCreateImageMem(vg, 0, data, dataSize);
    std::free(data);
    if (!image) {
        setImageError(entry.name, t("image.decode_failed"));
        return true;
    }

    int w = 0;
    int h = 0;
    nvgImageSize(vg, image, &w, &h);

    closeImage(vg);
    lockScan();
    gImage.nvgImage = image;
    gImage.loaded = 1;
    gImage.error = 0;
    gImage.width = w;
    gImage.height = h;
    gImage.rotationDegrees = 0;
    gImage.hudVisible = preserveHud ? hudVisible : 1;
    gImage.zoom = fitImageScale(w, h, 0);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    copyText(gImage.fileName, sizeof(gImage.fileName), entry.name);
    std::snprintf(gImage.message, sizeof(gImage.message), "%dx%d", w, h);
    unlockScan();
    return true;
}

void copyText(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) return;
    if (!src) src = "";
    std::snprintf(dst, dstSize, "%s", src);
}

void lockScan() {
    if (gScanMutex >= 0) sceKernelLockMutex(gScanMutex, 1, nullptr);
}

void unlockScan() {
    if (gScanMutex >= 0) sceKernelUnlockMutex(gScanMutex, 1);
}

void setPlayerMessage(const char* message) {
    lockScan();
    copyText(gPlayer.message, sizeof(gPlayer.message), message);
    unlockScan();
    sceClibPrintf("[vita-smb-player] player: %s\n", gPlayer.message);
}

void setPlayerDetail(const char* detail) {
    lockScan();
    copyText(gPlayer.detail, sizeof(gPlayer.detail), detail);
    unlockScan();
    sceClibPrintf("[vita-smb-player] detail: %s\n", gPlayer.detail);
}

void setPlaybackError(const char* title, const char* reason) {
    if (gPlayer.mpv) {
        const char* stopCmd[] = {"stop", nullptr};
        mpv_command(gPlayer.mpv, stopCmd);
    }
    lockScan();
    copyText(gPlayer.errorTitle, sizeof(gPlayer.errorTitle), title);
    copyText(gPlayer.errorReason, sizeof(gPlayer.errorReason), reason);
    gPlayer.loading = 0;
    gPlayer.waitingForValidation = 0;
    unlockScan();
    sceClibPrintf("[vita-smb-player] playback error: %s / %s\n", title, reason);
}

void pushPlayerLog(const char* line) {
    if (!line || !line[0]) return;
    lockScan();
    if (gPlayer.logCount < kPlayerLogLines) {
        copyText(gPlayer.logLines[gPlayer.logCount], sizeof(gPlayer.logLines[gPlayer.logCount]), line);
        ++gPlayer.logCount;
    } else {
        for (int i = 1; i < kPlayerLogLines; ++i) {
            copyText(gPlayer.logLines[i - 1], sizeof(gPlayer.logLines[i - 1]), gPlayer.logLines[i]);
        }
        copyText(gPlayer.logLines[kPlayerLogLines - 1], sizeof(gPlayer.logLines[kPlayerLogLines - 1]), line);
    }
    if (gPlayer.logScroll > 0) ++gPlayer.logScroll;
    if (gPlayer.logScroll > gPlayer.logCount - 1) gPlayer.logScroll = gPlayer.logCount - 1;
    unlockScan();
    sceClibPrintf("[vita-smb-player] log: %s\n", line);
}

void showPlayerOverlay() {
    lockScan();
    gPlayer.overlayFrames = 240;
    unlockScan();
}

void hidePlayerOverlay() {
    lockScan();
    gPlayer.overlayFrames = 0;
    unlockScan();
}

void tickPlayerOverlay() {
    lockScan();
    if (gPlayer.overlayFrames > 0) --gPlayer.overlayFrames;
    unlockScan();
}

double getMpvDouble(const char* name) {
    double value = 0.0;
    if (gPlayer.mpv) mpv_get_property(gPlayer.mpv, name, MPV_FORMAT_DOUBLE, &value);
    return value;
}

int getMpvFlag(const char* name) {
    int value = 0;
    if (gPlayer.mpv) mpv_get_property(gPlayer.mpv, name, MPV_FORMAT_FLAG, &value);
    return value;
}

int getMpvIntProperty(const char* name) {
    int64_t value = 0;
    if (gPlayer.mpv && mpv_get_property(gPlayer.mpv, name, MPV_FORMAT_INT64, &value) >= 0) {
        if (value > 0 && value < 16384) return static_cast<int>(value);
    }
    return 0;
}

void setMpvPause(bool paused) {
    if (!gPlayer.mpv) return;
    int flag = paused ? 1 : 0;
    mpv_set_property(gPlayer.mpv, "pause", MPV_FORMAT_FLAG, &flag);
    lockScan();
    gPlayer.paused = flag;
    copyText(gPlayer.message, sizeof(gPlayer.message), paused ? t("player.paused") : t("player.resumed"));
    unlockScan();
}

void setMpvRotation(int degrees) {
    if (degrees < 0) degrees = 0;
    degrees %= 360;
    const int normalized = (degrees / 90) * 90;
    int64_t value = normalized;
    if (gPlayer.mpv) {
        mpv_set_property(gPlayer.mpv, "video-rotate", MPV_FORMAT_INT64, &value);
    }
    lockScan();
    gPlayer.rotationDegrees = normalized;
    unlockScan();
}

void cycleMpvRotation() {
    int current = 0;
    lockScan();
    current = gPlayer.rotationDegrees;
    unlockScan();
    setMpvRotation(current == 270 ? 0 : 270);
    showPlayerOverlay();
}

void seekMpvRelative(double seconds) {
    if (!gPlayer.mpv) return;
    char delta[32];
    std::snprintf(delta, sizeof(delta), "%.1f", seconds);
    const char* cmd[] = {"seek", delta, "relative", "exact", nullptr};
    mpv_command(gPlayer.mpv, cmd);
}

void seekMpvAbsolute(double seconds) {
    if (!gPlayer.mpv) return;
    if (seconds < 0.0) seconds = 0.0;
    char target[32];
    std::snprintf(target, sizeof(target), "%.1f", seconds);
    const char* cmd[] = {"seek", target, "absolute", "exact", nullptr};
    mpv_command(gPlayer.mpv, cmd);
}

void stopCurrentPlayback() {
    if (gPlayer.mpv) {
        const char* stopCmd[] = {"stop", nullptr};
        mpv_command(gPlayer.mpv, stopCmd);
    }
    lockScan();
    gPlayer.loading = 0;
    gPlayer.hasFrame = 0;
    gPlayer.paused = 0;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.overlayFrames = 0;
    gPlayer.waitingForValidation = 0;
    copyText(gPlayer.message, sizeof(gPlayer.message), t("player.stopped_short"));
    unlockScan();
}

bool resolutionWithinVitaLimit(int width, int height) {
    if (width <= 0 || height <= 0) return true;
    const int shortSide = width < height ? width : height;
    const int longSide = width > height ? width : height;
    return shortSide <= 1080 && longSide <= 1920;
}

void buildResolutionErrorReason(char* out, size_t outSize, int width, int height) {
    std::snprintf(out, outSize,
                  t("error.resolution.reason.fmt"),
                  width, height);
}

void applyDetectedVideoDimensions(int width, int height) {
    if (width <= 0 || height <= 0) return;

    bool rejectResolution = false;
    bool resumePlayback = false;
    char rejectReason[256] = {};

    lockScan();
    gPlayer.videoWidth = width;
    gPlayer.videoHeight = height;
    if (gPlayer.waitingForValidation && !gPlayer.errorTitle[0]) {
        gPlayer.waitingForValidation = 0;
        if (!resolutionWithinVitaLimit(width, height)) {
            rejectResolution = true;
            buildResolutionErrorReason(rejectReason, sizeof(rejectReason), width, height);
        } else {
            resumePlayback = true;
        }
    }
    unlockScan();

    if (rejectResolution) {
        setPlaybackError(t("error.resolution.title"), rejectReason);
    } else if (resumePlayback) {
        setMpvPause(false);
    }
}

void updatePlaybackProperties() {
    if (!gPlayer.mpv) return;
    const double pos = getMpvDouble("time-pos");
    const double dur = getMpvDouble("duration");
    const int paused = getMpvFlag("pause");
    int width = getMpvIntProperty("video-params/w");
    int height = getMpvIntProperty("video-params/h");
    if (width <= 0) width = getMpvIntProperty("width");
    if (height <= 0) height = getMpvIntProperty("height");

    lockScan();
    if (pos >= 0.0 && pos < 24.0 * 60.0 * 60.0) gPlayer.positionSeconds = pos;
    if (dur > 0.0 && dur < 24.0 * 60.0 * 60.0) gPlayer.durationSeconds = dur;
    gPlayer.paused = paused;
    unlockScan();

    if (width > 0 && height > 0) applyDetectedVideoDimensions(width, height);
}

void setScanMessage(int phase, const char* message) {
    lockScan();
    gScanState.phase = phase;
    copyText(gScanState.message, sizeof(gScanState.message), message);
    unlockScan();
}

void setCopyMessage(const char* message, int error, int done) {
    lockScan();
    copyText(gCopyState.message, sizeof(gCopyState.message), message);
    gCopyState.error = error;
    gCopyState.done = done;
    if (done) gCopyState.busy = 0;
    unlockScan();
}

void updateCopyProgress(uint64_t copied, uint64_t total) {
    lockScan();
    gCopyState.copied = copied;
    gCopyState.total = total;
    unlockScan();
}

bool copyIsBusy() {
    lockScan();
    const bool busy = gCopyState.busy != 0;
    unlockScan();
    return busy;
}

void resetScanEntries(const char* message) {
    lockScan();
    gScanState.phase = ScanLoading;
    gScanState.source = gBrowserSource;
    gScanState.count = 0;
    copyText(gScanState.path, sizeof(gScanState.path), gCurrentPath);
    copyText(gScanState.message, sizeof(gScanState.message), message);
    unlockScan();
}

char asciiLower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool endsWithNoCase(const char* text, const char* suffix) {
    const size_t textLen = std::strlen(text);
    const size_t suffixLen = std::strlen(suffix);
    if (suffixLen > textLen) return false;

    const char* start = text + textLen - suffixLen;
    for (size_t i = 0; i < suffixLen; ++i) {
        if (asciiLower(start[i]) != asciiLower(suffix[i])) return false;
    }
    return true;
}

bool containsNoCase(const char* text, const char* needle) {
    if (!text || !needle || !needle[0]) return false;
    const size_t needleLen = std::strlen(needle);
    for (const char* p = text; *p; ++p) {
        size_t i = 0;
        while (i < needleLen && p[i] && asciiLower(p[i]) == asciiLower(needle[i])) ++i;
        if (i == needleLen) return true;
    }
    return false;
}

bool equalsNoCase(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (asciiLower(*a) != asciiLower(*b)) return false;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

bool isHiddenOrSystemName(const char* name) {
    if (!name || !name[0]) return true;
    if (name[0] == '.') return true;
    if (equalsNoCase(name, "$RECYCLE.BIN")) return true;
    if (equalsNoCase(name, "System Volume Information")) return true;
    if (equalsNoCase(name, "desktop.ini")) return true;
    if (equalsNoCase(name, "Thumbs.db")) return true;
    if (equalsNoCase(name, "ehthumbs.db")) return true;
    if (equalsNoCase(name, "@eaDir")) return true;
    if (equalsNoCase(name, "__MACOSX")) return true;
    if (equalsNoCase(name, "lost+found")) return true;
    if (std::strncmp(name, "._", 2) == 0) return true;
    return false;
}

bool isVideoFile(const char* name) {
    return endsWithNoCase(name, ".mp4") ||
           endsWithNoCase(name, ".m4v") ||
           endsWithNoCase(name, ".mkv") ||
           endsWithNoCase(name, ".avi") ||
           endsWithNoCase(name, ".mov") ||
           endsWithNoCase(name, ".webm") ||
           endsWithNoCase(name, ".ts");
}

bool isImageFile(const char* name) {
    return endsWithNoCase(name, ".jpg") ||
           endsWithNoCase(name, ".jpeg") ||
           endsWithNoCase(name, ".png") ||
           endsWithNoCase(name, ".bmp") ||
           endsWithNoCase(name, ".gif") ||
           endsWithNoCase(name, ".tga");
}

bool isMediaFile(const char* name) {
    return isVideoFile(name) || isImageFile(name);
}

int compareEntry(const SmbEntry& a, const SmbEntry& b) {
    if (a.directory != b.directory) return b.directory - a.directory;
    for (size_t i = 0; a.name[i] || b.name[i]; ++i) {
        const char ac = asciiLower(a.name[i]);
        const char bc = asciiLower(b.name[i]);
        if (ac != bc) return ac < bc ? -1 : 1;
    }
    return 0;
}

void sortScanEntries() {
    for (int i = 0; i < gScanState.count; ++i) {
        for (int j = i + 1; j < gScanState.count; ++j) {
            if (compareEntry(gScanState.entries[j], gScanState.entries[i]) < 0) {
                const SmbEntry tmp = gScanState.entries[i];
                gScanState.entries[i] = gScanState.entries[j];
                gScanState.entries[j] = tmp;
            }
        }
    }
}

void addScanEntry(const char* name, int directory, uint64_t size) {
    lockScan();
    if (gScanState.count < kMaxEntries) {
        SmbEntry& entry = gScanState.entries[gScanState.count++];
        copyText(entry.name, sizeof(entry.name), name);
        entry.directory = directory;
        entry.size = size;
    }
    unlockScan();
}

void finishScan(const char* message) {
    lockScan();
    sortScanEntries();
    gScanState.phase = ScanReady;
    copyText(gScanState.message, sizeof(gScanState.message), message);
    unlockScan();
}

RuntimeStatus probeLinkedLibraries() {
    RuntimeStatus status;

    struct smb2_context* smb = smb2_init_context();
    status.smb2ContextOk = smb != nullptr;
    if (smb) {
        smb2_destroy_context(smb);
    }

    status.mpvClientApi = mpv_client_api_version();
    return status;
}

bool initNetwork() {
    sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    sceSysmoduleLoadModule(SCE_SYSMODULE_SSL);

    gNetMem = std::malloc(kNetMemSize);
    if (!gNetMem) return false;

    SceNetInitParam netParam = {};
    netParam.memory = gNetMem;
    netParam.size = kNetMemSize;
    netParam.flags = 0;

    if (sceNetInit(&netParam) < 0) {
        std::free(gNetMem);
        gNetMem = nullptr;
        return false;
    }

    if (sceNetCtlInit() < 0) {
        sceNetTerm();
        std::free(gNetMem);
        gNetMem = nullptr;
        return false;
    }
    return true;
}

void shutdownNetwork() {
    sceNetCtlTerm();
    sceNetTerm();
    sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
    if (gNetMem) {
        std::free(gNetMem);
        gNetMem = nullptr;
    }
}

const char* endFileReasonName(mpv_end_file_reason reason) {
    switch (reason) {
    case MPV_END_FILE_REASON_EOF: return "eof";
    case MPV_END_FILE_REASON_STOP: return "stop";
    case MPV_END_FILE_REASON_QUIT: return "quit";
    case MPV_END_FILE_REASON_ERROR: return "error";
    case MPV_END_FILE_REASON_REDIRECT: return "redirect";
    default: return "unknown";
    }
}

void copyLogLine(char* dst, size_t dstSize, const char* prefix, const char* text) {
    if (dstSize == 0) return;
    std::snprintf(dst, dstSize, "%s%s", prefix ? prefix : "", text ? text : "");
    for (size_t i = 0; dst[i]; ++i) {
        if (dst[i] == '\r' || dst[i] == '\n') {
            dst[i] = '\0';
            break;
        }
    }
}

bool hasVisibleText(const char* text) {
    if (!text) return false;
    while (*text) {
        if (*text != ' ' && *text != '\t' && *text != '\r' && *text != '\n') return true;
        ++text;
    }
    return false;
}

bool isInterestingMpvLog(const mpv_event_log_message* log) {
    if (!log || !log->prefix || !log->text) return false;
    if (!hasVisibleText(log->text)) return false;
    if (log->log_level <= MPV_LOG_LEVEL_WARN) return true;

    const char* p = log->prefix;
    if (std::strcmp(p, "cplayer") == 0 ||
        std::strcmp(p, "demux") == 0 ||
        std::strcmp(p, "ffmpeg") == 0 ||
        std::strcmp(p, "vd") == 0 ||
        std::strcmp(p, "ad") == 0) {
        return true;
    }

    return std::strstr(log->text, "Video") ||
           std::strstr(log->text, "Audio") ||
           std::strstr(log->text, "codec") ||
           std::strstr(log->text, "stream") ||
           std::strstr(log->text, "Track");
}

const char* detectVideoCodec(const char* text) {
    if (containsNoCase(text, "vp9")) return "VP9";
    if (containsNoCase(text, "vp8")) return "VP8";
    if (containsNoCase(text, "av1")) return "AV1";
    if (containsNoCase(text, "hevc") ||
        containsNoCase(text, "h265") ||
        containsNoCase(text, "h.265")) {
        return "HEVC/H.265";
    }
    if (containsNoCase(text, "h264") ||
        containsNoCase(text, "h.264")) {
        return "H.264";
    }
    if (containsNoCase(text, "mpeg4")) return "MPEG-4";
    return nullptr;
}

const char* detectAudioCodec(const char* text) {
    if (containsNoCase(text, "opus")) return "Opus";
    if (containsNoCase(text, "truehd")) return "TrueHD";
    if (containsNoCase(text, "eac3")) return "E-AC3";
    if (containsNoCase(text, "ac3")) return "AC3";
    if (containsNoCase(text, "dts")) return "DTS";
    if (containsNoCase(text, "flac")) return "FLAC";
    if (containsNoCase(text, "aac")) return "AAC";
    if (containsNoCase(text, "mp3")) return "MP3";
    return nullptr;
}

bool parseResolutionFromText(const char* text, int* width, int* height) {
    if (!text || !width || !height) return false;
    for (const char* p = text; *p; ++p) {
        if (*p < '0' || *p > '9') continue;

        int a = 0;
        const char* q = p;
        while (*q >= '0' && *q <= '9') {
            a = a * 10 + (*q - '0');
            ++q;
        }
        if ((*q != 'x' && *q != 'X') || q == p) continue;
        ++q;
        if (*q < '0' || *q > '9') continue;

        int b = 0;
        const char* r = q;
        while (*r >= '0' && *r <= '9') {
            b = b * 10 + (*r - '0');
            ++r;
        }
        if (a >= 160 && b >= 90 && a < 10000 && b < 10000) {
            *width = a;
            *height = b;
            return true;
        }
    }
    return false;
}

void buildUnsupportedReason(char* out, size_t outSize, const char* fallback) {
    if (gPlayer.videoCodec[0] && gPlayer.audioCodec[0]) {
        std::snprintf(out, outSize, t("error.unsupported.video_audio.fmt"),
                      gPlayer.videoCodec, gPlayer.audioCodec);
    } else if (gPlayer.videoCodec[0]) {
        std::snprintf(out, outSize, t("error.unsupported.video.fmt"),
                      gPlayer.videoCodec);
    } else if (gPlayer.audioCodec[0]) {
        std::snprintf(out, outSize, t("error.unsupported.audio.fmt"),
                      gPlayer.audioCodec);
    } else {
        copyText(out, outSize, fallback);
    }
}

void refreshUnsupportedErrorReason() {
    char reason[256];
    lockScan();
    if (std::strcmp(gPlayer.errorTitle, t("error.format.title")) == 0 ||
        std::strcmp(gPlayer.errorTitle, t("error.codec.title")) == 0) {
        buildUnsupportedReason(reason, sizeof(reason), gPlayer.errorReason);
        copyText(gPlayer.errorReason, sizeof(gPlayer.errorReason), reason);
    }
    unlockScan();
}

void inspectMpvLogForCodec(const char* prefix, const char* text) {
    if (!text) return;
    int width = 0;
    int height = 0;
    if (parseResolutionFromText(text, &width, &height)) {
        applyDetectedVideoDimensions(width, height);
    }

    const char* video = detectVideoCodec(text);
    const char* audio = detectAudioCodec(text);
    if (!video && !audio) return;

    const bool videoLine = containsNoCase(prefix, "vd") || containsNoCase(text, "video") || video;
    const bool audioLine = containsNoCase(prefix, "ad") || containsNoCase(text, "audio") || audio;

    lockScan();
    if (video && videoLine && !gPlayer.videoCodec[0]) {
        copyText(gPlayer.videoCodec, sizeof(gPlayer.videoCodec), video);
    }
    if (audio && audioLine && !gPlayer.audioCodec[0]) {
        copyText(gPlayer.audioCodec, sizeof(gPlayer.audioCodec), audio);
    }
    unlockScan();

    refreshUnsupportedErrorReason();
}

void setUnsupportedPlaybackError(const char* title, const char* fallback) {
    char reason[256];
    if (gPlayer.mpv) {
        const char* stopCmd[] = {"stop", nullptr};
        mpv_command(gPlayer.mpv, stopCmd);
    }
    lockScan();
    buildUnsupportedReason(reason, sizeof(reason), fallback);
    copyText(gPlayer.errorTitle, sizeof(gPlayer.errorTitle), title);
    copyText(gPlayer.errorReason, sizeof(gPlayer.errorReason), reason);
    gPlayer.loading = 0;
    gPlayer.waitingForValidation = 0;
    unlockScan();
    sceClibPrintf("[vita-smb-player] playback error: %s / %s\n", title, reason);
}

bool shouldTreatDecoderInitAsFatal() {
    char video[64];
    char audio[64];
    int hasFrame = 0;
    double duration = 0.0;
    lockScan();
    copyText(video, sizeof(video), gPlayer.videoCodec);
    copyText(audio, sizeof(audio), gPlayer.audioCodec);
    hasFrame = gPlayer.hasFrame;
    duration = gPlayer.durationSeconds;
    unlockScan();

    if (hasFrame || duration > 0.0) return false;
    if (std::strcmp(video, "H.264") == 0 || std::strcmp(video, "MPEG-4") == 0) return false;
    if (std::strcmp(audio, "AAC") == 0 || std::strcmp(audio, "MP3") == 0 ||
        std::strcmp(audio, "FLAC") == 0) {
        return false;
    }
    return video[0] || audio[0];
}

void inspectMpvLogForUserError(const char* text) {
    if (!text) return;
    if (std::strstr(text, "No video or audio streams selected")) {
        setUnsupportedPlaybackError(t("error.format.title"), t("error.no_stream"));
    } else if (std::strstr(text, "Cannot find codec") ||
               std::strstr(text, "Failed to initialize decoder")) {
        if (shouldTreatDecoderInitAsFatal()) {
            setUnsupportedPlaybackError(t("error.codec.title"), t("error.codec.reason"));
        }
    } else if (std::strstr(text, "Failed to recognize file format")) {
        setPlaybackError(t("error.container.title"), t("error.container.reason"));
    }
}

void closeSmbStream(SmbStream* stream) {
    if (!stream) return;
    if (stream->fh) {
        smb2_close(stream->smb, stream->fh);
        stream->fh = nullptr;
    }
    if (stream->smb) {
        smb2_disconnect_share(stream->smb);
        smb2_destroy_context(stream->smb);
        stream->smb = nullptr;
    }
    delete stream;
}

int64_t smbStreamRead(void* cookie, char* buf, uint64_t nbytes) {
    SmbStream* stream = static_cast<SmbStream*>(cookie);
    if (!stream || !stream->smb || !stream->fh || stream->cancelled) return -1;

    uint64_t remaining = nbytes;
    char* cursor = buf;
    int64_t total = 0;

    while (remaining > 0) {
        uint32_t chunk = remaining > 256 * 1024 ? 256 * 1024 : static_cast<uint32_t>(remaining);
        const int rc = smb2_read(stream->smb, stream->fh, reinterpret_cast<uint8_t*>(cursor), chunk);
        if (rc < 0) return total > 0 ? total : -1;
        if (rc == 0) break;
        cursor += rc;
        total += rc;
        remaining -= static_cast<uint32_t>(rc);
        if (static_cast<uint32_t>(rc) < chunk) break;
    }

    return total;
}

int64_t smbStreamSeek(void* cookie, int64_t offset) {
    SmbStream* stream = static_cast<SmbStream*>(cookie);
    if (!stream || !stream->smb || !stream->fh || stream->cancelled) return MPV_ERROR_GENERIC;

    uint64_t current = 0;
    const int64_t rc = smb2_lseek(stream->smb, stream->fh, offset, SEEK_SET, &current);
    if (rc < 0) return MPV_ERROR_GENERIC;
    return static_cast<int64_t>(current);
}

int64_t smbStreamSize(void* cookie) {
    SmbStream* stream = static_cast<SmbStream*>(cookie);
    if (!stream || stream->size < 0) return MPV_ERROR_UNSUPPORTED;
    return stream->size;
}

void smbStreamClose(void* cookie) {
    closeSmbStream(static_cast<SmbStream*>(cookie));
}

void smbStreamCancel(void* cookie) {
    SmbStream* stream = static_cast<SmbStream*>(cookie);
    if (stream) stream->cancelled = 1;
}

int smbStreamOpen(void*, char* uri, mpv_stream_cb_info* info) {
    constexpr const char* kPrefix = "vitasmb://";
    constexpr size_t kPrefixLen = 10;
    if (!uri || std::strncmp(uri, kPrefix, kPrefixLen) != 0) {
        return MPV_ERROR_LOADING_FAILED;
    }

    const char* path = uri + kPrefixLen;
    while (*path == '/') ++path;
    if (!path[0]) return MPV_ERROR_LOADING_FAILED;

    SmbStream* stream = new SmbStream();
    stream->smb = nullptr;
    stream->fh = nullptr;
    stream->size = -1;
    stream->cancelled = 0;

    stream->smb = smb2_init_context();
    if (!stream->smb) {
        delete stream;
        return MPV_ERROR_LOADING_FAILED;
    }

    smb2_set_timeout(stream->smb, 10);
    smb2_set_security_mode(stream->smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (optionalText(gConn.domain)) smb2_set_domain(stream->smb, gConn.domain);
    if (optionalText(gConn.password)) smb2_set_password(stream->smb, gConn.password);

    if (smb2_connect_share(stream->smb, gConn.server, gConn.share, optionalText(gConn.user)) < 0) {
        setPlayerMessage(smb2_get_error(stream->smb));
        closeSmbStream(stream);
        return MPV_ERROR_LOADING_FAILED;
    }

    stream->fh = smb2_open(stream->smb, path, O_RDONLY);
    if (!stream->fh) {
        setPlayerMessage(smb2_get_error(stream->smb));
        closeSmbStream(stream);
        return MPV_ERROR_LOADING_FAILED;
    }

    struct smb2_stat_64 st = {};
    if (smb2_fstat(stream->smb, stream->fh, &st) == 0) {
        stream->size = static_cast<int64_t>(st.smb2_size);
    }

    info->cookie = stream;
    info->read_fn = smbStreamRead;
    info->seek_fn = smbStreamSeek;
    info->size_fn = smbStreamSize;
    info->close_fn = smbStreamClose;
    info->cancel_fn = smbStreamCancel;

    char msg[192];
    std::snprintf(msg, sizeof(msg), t("player.streaming.fmt"), path);
    setPlayerMessage(msg);
    return 0;
}

bool initMpv(SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher, SceGxmMultisampleMode msaa, NVGcontext* vg) {
    if (gPlayer.initialized) return true;

    std::setlocale(LC_NUMERIC, "C");
    gPlayer.vg = vg;
    gPlayer.flipY = 1;

    gPlayer.mpv = mpv_create();
    if (!gPlayer.mpv) {
        setPlayerMessage("mpv_create failed");
        return false;
    }

    mpv_set_option_string(gPlayer.mpv, "msg-level", "all=info");
    mpv_set_option_string(gPlayer.mpv, "config", "no");
    mpv_set_option_string(gPlayer.mpv, "idle", "yes");
    mpv_set_option_string(gPlayer.mpv, "keep-open", "yes");
    mpv_set_option_string(gPlayer.mpv, "terminal", "no");
    mpv_set_option_string(gPlayer.mpv, "vo", "libmpv");
    mpv_set_option_string(gPlayer.mpv, "hwdec", "vita-copy");
    mpv_set_option_string(gPlayer.mpv, "audio-channels", "stereo");
    mpv_set_option_string(gPlayer.mpv, "vid", "auto");
    mpv_set_option_string(gPlayer.mpv, "aid", "auto");
    mpv_set_option_string(gPlayer.mpv, "sid", "no");
    mpv_set_option_string(gPlayer.mpv, "vd-lavc-threads", "4");
    mpv_set_option_string(gPlayer.mpv, "fbo-format", "rgba8");
    mpv_set_option_string(gPlayer.mpv, "video-latency-hacks", "yes");
    mpv_set_option_string(gPlayer.mpv, "demuxer-lavf-analyzeduration", "0.4");
    mpv_set_option_string(gPlayer.mpv, "demuxer-lavf-probescore", "24");
    mpv_set_option_string(gPlayer.mpv, "cache", "yes");
    mpv_set_option_string(gPlayer.mpv, "demuxer-max-bytes", "32MiB");
    mpv_set_option_string(gPlayer.mpv, "demuxer-max-back-bytes", "16MiB");
    mpv_set_option_string(gPlayer.mpv, "demuxer-readahead-secs", "5");

    int rc = mpv_stream_cb_add_ro(gPlayer.mpv, "vitasmb", nullptr, smbStreamOpen);
    if (rc < 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "stream_cb failed: %s", mpv_error_string(rc));
        setPlayerMessage(msg);
        mpv_terminate_destroy(gPlayer.mpv);
        gPlayer.mpv = nullptr;
        return false;
    }

    mpv_gxm_init_params gxmParams = {};
    gxmParams.context = gxmCtx;
    gxmParams.shader_patcher = patcher;
    gxmParams.buffer_index = 0;
    gxmParams.msaa = msaa;

    mpv_render_param initParams[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_GXM)},
        {MPV_RENDER_PARAM_GXM_INIT_PARAMS, &gxmParams},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    rc = mpv_render_context_create(&gPlayer.renderCtx, gPlayer.mpv, initParams);
    if (rc < 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "render context failed: %s", mpv_error_string(rc));
        setPlayerMessage(msg);
        mpv_terminate_destroy(gPlayer.mpv);
        gPlayer.mpv = nullptr;
        return false;
    }

    rc = mpv_initialize(gPlayer.mpv);
    if (rc < 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "mpv_initialize failed: %s", mpv_error_string(rc));
        setPlayerMessage(msg);
        mpv_render_context_free(gPlayer.renderCtx);
        gPlayer.renderCtx = nullptr;
        mpv_terminate_destroy(gPlayer.mpv);
        gPlayer.mpv = nullptr;
        return false;
    }

    const int texW = DISPLAY_WIDTH;
    const int texH = DISPLAY_HEIGHT;
    const int texStride = ALIGN(texW, 8);
    gPlayer.nvgImage = nvgCreateImageRGBA(vg, texW, texH, 0, nullptr);
    NVGXMtexture* texture = nvgxmImageHandle(vg, gPlayer.nvgImage);
    if (!texture) {
        setPlayerMessage("nvgxmImageHandle failed");
        return false;
    }

    NVGXMframebufferInitOptions fbOpts = {};
    fbOpts.display_buffer_count = 1;
    fbOpts.scenesPerFrame = 1;
    fbOpts.render_target = texture;
    fbOpts.color_format = SCE_GXM_COLOR_FORMAT_U8U8U8U8_ABGR;
    fbOpts.color_surface_type = SCE_GXM_COLOR_SURFACE_LINEAR;
    fbOpts.display_width = texW;
    fbOpts.display_height = texH;
    fbOpts.display_stride = texStride;

    gPlayer.fbo = gxmCreateFramebuffer(&fbOpts);
    if (!gPlayer.fbo) {
        setPlayerMessage("gxmCreateFramebuffer failed");
        return false;
    }

    gPlayer.mpvFbo.render_target = gPlayer.fbo->gxm_render_target;
    gPlayer.mpvFbo.color_surface = &gPlayer.fbo->gxm_color_surfaces[0].surface;
    gPlayer.mpvFbo.depth_stencil_surface = &gPlayer.fbo->gxm_depth_stencil_surface;
    gPlayer.mpvFbo.w = texW;
    gPlayer.mpvFbo.h = texH;
    gPlayer.mpvFbo.format = SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_RGBA;

    gPlayer.mpvParams[0] = {MPV_RENDER_PARAM_FLIP_Y, &gPlayer.flipY};
    gPlayer.mpvParams[1] = {MPV_RENDER_PARAM_GXM_FBO, &gPlayer.mpvFbo};
    gPlayer.mpvParams[2] = {MPV_RENDER_PARAM_INVALID, nullptr};

    gPlayer.initialized = 1;
    mpv_request_log_messages(gPlayer.mpv, "info");
    setPlayerMessage(t("player.ready"));
    return true;
}

void shutdownMpv() {
    if (gPlayer.mpv) {
        const char* stopCmd[] = {"stop", nullptr};
        mpv_command(gPlayer.mpv, stopCmd);
        for (int i = 0; i < 8; ++i) {
            mpv_event* event = mpv_wait_event(gPlayer.mpv, 0.0);
            if (!event || event->event_id == MPV_EVENT_NONE) break;
        }
    }
    if (gPlayer.renderCtx) {
        mpv_render_context_free(gPlayer.renderCtx);
        gPlayer.renderCtx = nullptr;
    }
    if (gPlayer.mpv) {
        mpv_terminate_destroy(gPlayer.mpv);
        gPlayer.mpv = nullptr;
    }
    if (gPlayer.fbo) {
        gxmDeleteFramebuffer(gPlayer.fbo);
        gPlayer.fbo = nullptr;
    }
    if (gPlayer.vg && gPlayer.nvgImage) {
        nvgDeleteImage(gPlayer.vg, gPlayer.nvgImage);
        gPlayer.nvgImage = 0;
    }
    gPlayer.initialized = 0;
    gPlayer.loading = 0;
    gPlayer.hasFrame = 0;
    gPlayer.paused = 0;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.videoWidth = 0;
    gPlayer.videoHeight = 0;
    gPlayer.rotationDegrees = 0;
    gPlayer.waitingForValidation = 0;
}

void renderMpvToFbo() {
    if (!gPlayer.renderCtx) return;
    if (!(mpv_render_context_update(gPlayer.renderCtx) & MPV_RENDER_UPDATE_FRAME)) return;
    mpv_render_context_render(gPlayer.renderCtx, gPlayer.mpvParams);
    mpv_render_context_report_swap(gPlayer.renderCtx);
    gPlayer.hasFrame = 1;
}

void drawVideo(NVGcontext* vg, float x, float y, float w, float h) {
    if (!gPlayer.nvgImage) return;
    NVGpaint img = nvgImagePattern(vg, x, y, w, h, 0.0f, gPlayer.nvgImage, 1.0f);
    nvgBeginPath(vg);
    nvgRect(vg, x, y, w, h);
    nvgFillPaint(vg, img);
    nvgFill(vg);
}

void playEntry(const SmbEntry& entry, int source, SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
               SceGxmMultisampleMode msaa, NVGcontext* vg) {
    if (entry.directory) {
        setPlayerMessage(t("player.dir_unavailable"));
        return;
    }
    if (!initMpv(gxmCtx, patcher, msaa, vg)) return;

    char url[320];
    if (source == SourceLocal) {
        buildLocalFilePath(entry.name, url, sizeof(url));
    } else {
        char path[256];
        buildSmbFilePath(entry.name, path, sizeof(path));
        std::snprintf(url, sizeof(url), "vitasmb://%s", path);
    }

    setMpvRotation(0);
    int pauseFlag = 1;
    mpv_set_property(gPlayer.mpv, "pause", MPV_FORMAT_FLAG, &pauseFlag);

    const char* cmd[] = {"loadfile", url, nullptr};
    const int rc = mpv_command(gPlayer.mpv, cmd);
    if (rc < 0) {
        char msg[160];
        std::snprintf(msg, sizeof(msg), "loadfile failed: %s", mpv_error_string(rc));
        setPlayerMessage(msg);
        return;
    }

    lockScan();
    gPlayer.loading = 1;
    gPlayer.hasFrame = 0;
    gPlayer.paused = 1;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.videoWidth = 0;
    gPlayer.videoHeight = 0;
    gPlayer.waitingForValidation = 1;
    copyText(gPlayer.fileName, sizeof(gPlayer.fileName), entry.name);
    copyText(gPlayer.message, sizeof(gPlayer.message), t("player.loadfile_sent"));
    copyText(gPlayer.detail, sizeof(gPlayer.detail), "");
    copyText(gPlayer.errorTitle, sizeof(gPlayer.errorTitle), "");
    copyText(gPlayer.errorReason, sizeof(gPlayer.errorReason), "");
    copyText(gPlayer.videoCodec, sizeof(gPlayer.videoCodec), "");
    copyText(gPlayer.audioCodec, sizeof(gPlayer.audioCodec), "");
    gPlayer.logCount = 0;
    gPlayer.logScroll = 0;
    gPlayer.overlayFrames = 240;
    for (int i = 0; i < kPlayerLogLines; ++i) {
        gPlayer.logLines[i][0] = '\0';
    }
    unlockScan();
}

void pollMpvEvents() {
    if (!gPlayer.mpv) return;
    for (int i = 0; i < 64; ++i) {
        mpv_event* event = mpv_wait_event(gPlayer.mpv, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) break;

        if (event->event_id == MPV_EVENT_LOG_MESSAGE) {
            mpv_event_log_message* log = static_cast<mpv_event_log_message*>(event->data);
            if (isInterestingMpvLog(log)) {
                inspectMpvLogForCodec(log->prefix, log->text);
                inspectMpvLogForUserError(log->text);
                if (log->log_level <= MPV_LOG_LEVEL_WARN) {
                    char detail[192];
                    char prefix[48];
                    std::snprintf(prefix, sizeof(prefix), "%s/%s: ",
                                  log->prefix ? log->prefix : "mpv",
                                  log->level ? log->level : "?");
                    copyLogLine(detail, sizeof(detail), prefix, log->text);
                    pushPlayerLog(detail);
                }
            }
        } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            lockScan();
            gPlayer.loading = 0;
            copyText(gPlayer.message, sizeof(gPlayer.message), t("player.playing"));
            copyText(gPlayer.detail, sizeof(gPlayer.detail), "");
            unlockScan();
        } else if (event->event_id == MPV_EVENT_FILE_LOADED) {
            lockScan();
            gPlayer.loading = 0;
            copyText(gPlayer.message, sizeof(gPlayer.message), t("player.loaded"));
            copyText(gPlayer.detail, sizeof(gPlayer.detail), "");
            unlockScan();
        } else if (event->event_id == MPV_EVENT_END_FILE) {
            mpv_event_end_file* end = static_cast<mpv_event_end_file*>(event->data);
            char detail[192];
            if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
                std::snprintf(detail, sizeof(detail), "end-file: %s (%s)",
                              endFileReasonName(end->reason), mpv_error_string(end->error));
            } else if (end) {
                std::snprintf(detail, sizeof(detail), "end-file: %s", endFileReasonName(end->reason));
            } else {
                std::snprintf(detail, sizeof(detail), "end-file: no detail");
            }
            lockScan();
            gPlayer.loading = 0;
            copyText(gPlayer.message, sizeof(gPlayer.message),
                     end && end->reason == MPV_END_FILE_REASON_EOF ? t("player.ended") : t("player.stopped"));
            copyText(gPlayer.detail, sizeof(gPlayer.detail), detail);
            unlockScan();
            pushPlayerLog(detail);
        } else if (event->event_id == MPV_EVENT_SHUTDOWN) {
            gPlayer.mpv = nullptr;
            gPlayer.initialized = 0;
            gPlayer.loading = 0;
            break;
        }
    }
    updatePlaybackProperties();
}

int scanSmbThread(SceSize, void*) {
    resetScanEntries(t("scan.smb.connecting"));

    struct smb2_context* smb = smb2_init_context();
    if (!smb) {
        setScanMessage(ScanError, t("scan.smb.init_failed"));
        return 1;
    }

    smb2_set_timeout(smb, 8);
    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (optionalText(gConn.domain)) smb2_set_domain(smb, gConn.domain);
    if (optionalText(gConn.password)) smb2_set_password(smb, gConn.password);

    if (smb2_connect_share(smb, gConn.server, gConn.share, optionalText(gConn.user)) < 0) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), t("scan.smb.connect_failed.fmt"), smb2_get_error(smb));
        setScanMessage(ScanError, msg);
        smb2_destroy_context(smb);
        return 1;
    }

    setScanMessage(ScanLoading, t("scan.smb.reading"));
    char pathBuf[256];
    getCurrentPath(pathBuf, sizeof(pathBuf));
    const char* path = pathBuf;
    struct smb2dir* dir = smb2_opendir(smb, path);
    if (!dir) {
        const char* firstError = smb2_get_error(smb);
        char savedError[128];
        copyText(savedError, sizeof(savedError), firstError);
        if (path[0] != '\0') {
            dir = smb2_opendir(smb, "");
        }
        if (!dir) {
            char msg[192];
            std::snprintf(msg, sizeof(msg), t("scan.smb.open_failed.fmt"), path, savedError);
            setScanMessage(ScanError, msg);
            smb2_disconnect_share(smb);
            smb2_destroy_context(smb);
            return 1;
        }
    }

    int skipped = 0;
    struct smb2dirent* ent = nullptr;
    while ((ent = smb2_readdir(smb, dir)) != nullptr) {
        if (!ent->name || std::strcmp(ent->name, ".") == 0 || std::strcmp(ent->name, "..") == 0) {
            continue;
        }
        if (isHiddenOrSystemName(ent->name)) {
            ++skipped;
            continue;
        }
        if (isUserHiddenItem(SourceSmb, gConn.server, gConn.share, path, ent->name)) {
            ++skipped;
            continue;
        }

        const int isDir = ent->st.smb2_type == SMB2_TYPE_DIRECTORY;
        if (!isDir && !isMediaFile(ent->name)) {
            ++skipped;
            continue;
        }

        addScanEntry(ent->name, isDir, ent->st.smb2_size);
    }

    smb2_closedir(smb, dir);
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);

    int loaded = 0;
    lockScan();
    loaded = gScanState.count;
    unlockScan();

    char msg[192];
    std::snprintf(msg, sizeof(msg), t("scan.smb.loaded.fmt"), loaded, skipped);
    finishScan(msg);
    return 0;
}

int scanLocalThread(SceSize, void*) {
    resetScanEntries(t("scan.local.reading"));

    char path[256];
    getCurrentPath(path, sizeof(path));
    SceUID dir = sceIoDopen(path);
    if (dir < 0) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), t("scan.local.open_failed.fmt"), static_cast<unsigned int>(dir));
        setScanMessage(ScanError, msg);
        return 1;
    }

    int skipped = 0;
    while (true) {
        SceIoDirent ent = {};
        const int rc = sceIoDread(dir, &ent);
        if (rc <= 0) break;
        if (!ent.d_name[0] || std::strcmp(ent.d_name, ".") == 0 || std::strcmp(ent.d_name, "..") == 0) {
            continue;
        }
        if (isHiddenOrSystemName(ent.d_name)) {
            ++skipped;
            continue;
        }
        if (isUserHiddenItem(SourceLocal, "", "", path, ent.d_name)) {
            ++skipped;
            continue;
        }

        const int isDir = SCE_S_ISDIR(ent.d_stat.st_mode);
        if (!isDir && !isMediaFile(ent.d_name)) {
            ++skipped;
            continue;
        }

        lockScan();
        if (gScanState.count < kMaxEntries) {
            SmbEntry& entry = gScanState.entries[gScanState.count++];
            copyText(entry.name, sizeof(entry.name), ent.d_name);
            entry.directory = isDir;
            entry.size = isDir ? 0 : static_cast<uint64_t>(ent.d_stat.st_size);
        }
        unlockScan();
    }
    sceIoDclose(dir);

    int loaded = 0;
    lockScan();
    sortScanEntries();
    loaded = gScanState.count;
    unlockScan();

    char msg[192];
    std::snprintf(msg, sizeof(msg), t("scan.local.loaded.fmt"), loaded, skipped);
    finishScan(msg);
    return 0;
}

int copySmbFileThread(SceSize args, void* arg) {
    CopyJob* job = nullptr;
    if (arg && args == sizeof(CopyJob*)) {
        std::memcpy(&job, arg, sizeof(job));
    }
    if (!job) return 1;

    sceIoMkdir(job->destDir, 0777);
    setCopyMessage(t("copy.connecting"), 0, 0);

    struct smb2_context* smb = smb2_init_context();
    if (!smb) {
        setCopyMessage(t("copy.init_failed"), 1, 1);
        delete job;
        return 1;
    }

    smb2_set_timeout(smb, 8);
    smb2_set_security_mode(smb, SMB2_NEGOTIATE_SIGNING_ENABLED);
    if (optionalText(gConn.domain)) smb2_set_domain(smb, gConn.domain);
    if (optionalText(gConn.password)) smb2_set_password(smb, gConn.password);

    if (smb2_connect_share(smb, gConn.server, gConn.share, optionalText(gConn.user)) < 0) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), t("copy.connect_failed.fmt"), smb2_get_error(smb));
        setCopyMessage(msg, 1, 1);
        smb2_destroy_context(smb);
        delete job;
        return 1;
    }

    struct smb2fh* src = smb2_open(smb, job->smbPath, O_RDONLY);
    if (!src) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), t("copy.open_failed.fmt"), smb2_get_error(smb));
        setCopyMessage(msg, 1, 1);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        delete job;
        return 1;
    }

    uint64_t total = job->total;
    struct smb2_stat_64 st = {};
    if (smb2_fstat(smb, src, &st) == 0 && st.smb2_size > 0) {
        total = static_cast<uint64_t>(st.smb2_size);
        updateCopyProgress(0, total);
    }

    SceUID dst = sceIoOpen(job->destPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (dst < 0) {
        char msg[192];
        std::snprintf(msg, sizeof(msg), t("copy.create_failed.fmt"), static_cast<unsigned int>(dst));
        setCopyMessage(msg, 1, 1);
        smb2_close(smb, src);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        delete job;
        return 1;
    }

    uint8_t* buffer = static_cast<uint8_t*>(std::malloc(256 * 1024));
    if (!buffer) {
        setCopyMessage(t("copy.oom"), 1, 1);
        sceIoClose(dst);
        smb2_close(smb, src);
        smb2_disconnect_share(smb);
        smb2_destroy_context(smb);
        delete job;
        return 1;
    }

    setCopyMessage(t("copy.running"), 0, 0);
    uint64_t copied = 0;
    int failed = 0;
    char failMsg[192] = {};
    while (true) {
        const int rc = smb2_read(smb, src, buffer, 256 * 1024);
        if (rc < 0) {
            std::snprintf(failMsg, sizeof(failMsg), t("copy.read_failed.fmt"), smb2_get_error(smb));
            failed = 1;
            break;
        }
        if (rc == 0) break;

        int written = 0;
        while (written < rc) {
            const SceSSize w = sceIoWrite(dst, buffer + written, static_cast<SceSize>(rc - written));
            if (w <= 0) {
                std::snprintf(failMsg, sizeof(failMsg), t("copy.write_failed.fmt"), static_cast<unsigned int>(w));
                failed = 1;
                break;
            }
            written += static_cast<int>(w);
        }
        if (failed) break;

        copied += static_cast<uint64_t>(rc);
        updateCopyProgress(copied, total);
    }

    std::free(buffer);
    sceIoClose(dst);
    smb2_close(smb, src);
    smb2_disconnect_share(smb);
    smb2_destroy_context(smb);

    if (failed) {
        setCopyMessage(failMsg, 1, 1);
    } else {
        updateCopyProgress(total > 0 ? total : copied, total > 0 ? total : copied);
        setCopyMessage(t("copy.complete"), 0, 1);
    }

    delete job;
    return failed ? 1 : 0;
}

void startScan() {
    lockScan();
    const bool busy = gScanState.phase == ScanLoading;
    const int source = gBrowserSource;
    unlockScan();
    if (busy) return;

    resetScanEntries(source == SourceLocal ? t("scan.local.start") : t("scan.smb.start"));
    SceUID thread = sceKernelCreateThread(source == SourceLocal ? "local_scan" : "smb_scan",
                                          source == SourceLocal ? scanLocalThread : scanSmbThread,
                                          0x60, 256 * 1024, 0, 0, nullptr);
    if (thread < 0) {
        setScanMessage(ScanError, t("scan.thread_failed"));
        return;
    }
    sceKernelStartThread(thread, 0, nullptr);
}

void startCopySelected(const SmbEntry& entry, int source) {
    if (source != SourceSmb || entry.directory) return;
    if (copyIsBusy()) return;

    CopyJob* job = new CopyJob();
    if (!job) {
        setCopyMessage(t("copy.oom"), 1, 1);
        return;
    }
    buildSmbFilePath(entry.name, job->smbPath, sizeof(job->smbPath));
    copyText(job->fileName, sizeof(job->fileName), entry.name);
    copyText(job->destDir, sizeof(job->destDir),
             isImageFile(entry.name) ? kLocalImageCopyRoot : kLocalVideoCopyRoot);
    std::snprintf(job->destPath, sizeof(job->destPath), "%s/%s", job->destDir, entry.name);
    job->total = entry.size;

    lockScan();
    gCopyState.busy = 1;
    gCopyState.done = 0;
    gCopyState.error = 0;
    gCopyState.copied = 0;
    gCopyState.total = entry.size;
    copyText(gCopyState.fileName, sizeof(gCopyState.fileName), entry.name);
    copyText(gCopyState.destPath, sizeof(gCopyState.destPath), job->destPath);
    copyText(gCopyState.message, sizeof(gCopyState.message), t("copy.preparing"));
    unlockScan();

    SceUID thread = sceKernelCreateThread("smb_copy", copySmbFileThread, 0x58, 512 * 1024, 0, 0, nullptr);
    if (thread < 0) {
        delete job;
        setCopyMessage(t("copy.thread_failed"), 1, 1);
        return;
    }
    sceKernelStartThread(thread, sizeof(CopyJob*), &job);
}

void drawText(NVGcontext* vg, int font, float x, float y, float size, NVGcolor color, const char* text) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, color);
    nvgText(vg, x, y, text, nullptr);
}

float textWidth(NVGcontext* vg, int font, float size, const char* text) {
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, size);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
    float bounds[4] = {};
    nvgTextBounds(vg, 0.0f, 0.0f, text ? text : "", nullptr, bounds);
    return bounds[2] - bounds[0];
}

void drawMarqueeText(NVGcontext* vg, int font, float x, float y, float maxW, float size,
                     NVGcolor color, const char* text, bool active) {
    if (!text || !text[0] || maxW <= 0.0f) return;

    const float width = textWidth(vg, font, size, text);
    float offset = 0.0f;
    if (active && width > maxW) {
        const float overflow = width - maxW;
        const float gap = 56.0f;
        const float speed = 0.65f;
        const int pauseFrames = 75;
        const int travelFrames = static_cast<int>((overflow + gap) / speed) + 1;
        const int cycle = pauseFrames + travelFrames;
        const int phase = cycle > 0 ? gUiFrame % cycle : 0;
        if (phase >= pauseFrames) {
            offset = (phase - pauseFrames) * speed;
            if (offset > overflow + gap) offset = overflow + gap;
        }
    }

    nvgSave(vg);
    nvgIntersectScissor(vg, x, y - size - 6.0f, maxW, size + 12.0f);
    drawText(vg, font, x - offset, y, size, color, text);
    nvgRestore(vg);
}

float clampFloat(float value, float lo, float hi);

const char* fileExtensionLabel(const char* name) {
    const char* dot = nullptr;
    for (const char* p = name ? name : ""; *p; ++p) {
        if (*p == '.') dot = p;
    }
    if (!dot || !dot[1]) return "VID";
    if (equalsNoCase(dot + 1, "mp4")) return "MP4";
    if (equalsNoCase(dot + 1, "m4v")) return "M4V";
    if (equalsNoCase(dot + 1, "mkv")) return "MKV";
    if (equalsNoCase(dot + 1, "avi")) return "AVI";
    if (equalsNoCase(dot + 1, "mov")) return "MOV";
    if (equalsNoCase(dot + 1, "webm")) return "WEBM";
    if (equalsNoCase(dot + 1, "ts")) return "TS";
    return "VID";
}

void drawFolderGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 4.0f, 30.0f, 20.0f, 3.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x + 2.0f, y, 13.0f, 8.0f, 2.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPlayGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgCircle(vg, x + 14.0f, y + 14.0f, 14.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 28));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 10.0f, y + 7.0f);
    nvgLineTo(vg, x + 21.0f, y + 14.0f);
    nvgLineTo(vg, x + 10.0f, y + 21.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawPhotoGlyph(NVGcontext* vg, float x, float y, NVGcolor color) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 2.0f, 30.0f, 24.0f, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 24));
    nvgFill(vg);
    nvgStrokeWidth(vg, 1.4f);
    nvgStrokeColor(vg, color);
    nvgStroke(vg);

    nvgBeginPath(vg);
    nvgCircle(vg, x + 22.0f, y + 9.0f, 3.0f);
    nvgFillColor(vg, color);
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgMoveTo(vg, x + 5.0f, y + 22.0f);
    nvgLineTo(vg, x + 13.0f, y + 13.0f);
    nvgLineTo(vg, x + 19.0f, y + 19.0f);
    nvgLineTo(vg, x + 25.0f, y + 14.0f);
    nvgLineTo(vg, x + 29.0f, y + 22.0f);
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);
}

void drawSmallBadge(NVGcontext* vg, int font, float x, float y, float w,
                    const char* label, NVGcolor bg, NVGcolor fg) {
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, 22.0f, 3.0f);
    nvgFillColor(vg, bg);
    nvgFill(vg);
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, fg);
    nvgText(vg, x + w * 0.5f, y + 11.0f, label, nullptr);
}

void drawEntryRow(NVGcontext* vg, int font, const SmbEntry& entry, int row, bool selected) {
    const float top = 178.0f + row * 38.0f;
    const float y = top + 26.0f;
    const float rowX = 40.0f;
    const float rowW = 880.0f;
    if (!selected && (row % 2) == 1) {
        nvgBeginPath(vg);
        nvgRect(vg, rowX, top, rowW, 36.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 10));
        nvgFill(vg);
    }
    if (selected) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, rowX, top, rowW, 36.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(30, 76, 99));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, rowX, top, 5.0f, 36.0f, 2.0f);
        nvgFillColor(vg, nvgRGB(0, 180, 216));
        nvgFill(vg);
    }

    const NVGcolor textColor = selected ? nvgRGB(250, 253, 255) : nvgRGB(219, 228, 232);
    if (entry.directory) {
        drawFolderGlyph(vg, 58.0f, top + 7.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(118, 177, 196));
        drawMarqueeText(vg, font, 108.0f, y, 800.0f, 21.0f, textColor, entry.name, selected);
    } else {
        const unsigned long mb = static_cast<unsigned long>((entry.size + 1024 * 1024 - 1) / (1024 * 1024));
        char sizeLine[40];
        std::snprintf(sizeLine, sizeof(sizeLine), "%lu MB", mb);
        if (isImageFile(entry.name)) {
            drawPhotoGlyph(vg, 58.0f, top + 4.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(156, 188, 201));
        } else {
            drawPlayGlyph(vg, 58.0f, top + 4.0f, selected ? nvgRGB(116, 221, 247) : nvgRGB(156, 188, 201));
        }
        drawSmallBadge(vg, font, 98.0f, top + 7.0f, 52.0f, fileExtensionLabel(entry.name),
                       selected ? nvgRGBA(0, 180, 216, 70) : nvgRGBA(255, 255, 255, 22),
                       selected ? nvgRGB(225, 248, 255) : nvgRGB(172, 190, 200));

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 18.0f);
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
        nvgFillColor(vg, textColor);
        nvgText(vg, 908.0f, y, sizeLine, nullptr);

        drawMarqueeText(vg, font, 168.0f, y, 620.0f, 21.0f, textColor, entry.name, selected);
    }
}

void drawHiddenRow(NVGcontext* vg, int font, const HiddenItem& item, int row, bool selected) {
    const float top = 178.0f + row * 38.0f;
    const float y = top + 26.0f;
    if (!selected && (row % 2) == 1) {
        nvgBeginPath(vg);
        nvgRect(vg, 40.0f, top, 880.0f, 36.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 10));
        nvgFill(vg);
    }
    if (selected) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 40.0f, top, 880.0f, 36.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(30, 76, 99));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 40.0f, top, 5.0f, 36.0f, 2.0f);
        nvgFillColor(vg, nvgRGB(0, 180, 216));
        nvgFill(vg);
    }

    const NVGcolor textColor = selected ? nvgRGB(250, 253, 255) : nvgRGB(219, 228, 232);
    char detail[384];
    if (item.source == SourceSmb) {
        drawText(vg, font, 56.0f, y, 19.0f, textColor, "SMB");
        std::snprintf(detail, sizeof(detail), "//%s/%s/%s   %s",
                      item.server, item.share, item.path, item.name);
    } else {
        drawText(vg, font, 56.0f, y, 19.0f, textColor, "LOCAL");
        std::snprintf(detail, sizeof(detail), "%s   %s", item.path, item.name);
    }
    drawMarqueeText(vg, font, 136.0f, y, 772.0f, 19.0f, textColor, detail, selected);
}

void drawCopyProgress(NVGcontext* vg, int font, const CopyState& copy) {
    if (!copy.busy && !copy.done && !copy.message[0]) return;

    const float x = 40.0f;
    const float y = 146.0f;
    const float w = 880.0f;
    const float h = 22.0f;
    float frac = 0.0f;
    if (copy.total > 0) {
        frac = static_cast<float>(static_cast<double>(copy.copied) / static_cast<double>(copy.total));
        frac = clampFloat(frac, 0.0f, 1.0f);
    }

    char line[256];
    const int percent = static_cast<int>(frac * 100.0f + 0.5f);
    if (copy.busy) {
        std::snprintf(line, sizeof(line), t("copy.busy.fmt"), percent, copy.fileName);
    } else if (copy.error) {
        std::snprintf(line, sizeof(line), t("copy.error.fmt"), copy.message);
    } else {
        std::snprintf(line, sizeof(line), t("copy.done.fmt"), copy.destPath);
    }

    drawText(vg, font, x, y, 15.0f,
             copy.error ? nvgRGB(245, 104, 104) : nvgRGB(190, 202, 210), line);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w, h, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 38));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y + 8.0f, w * (copy.done && !copy.error ? 1.0f : frac), h, 3.0f);
    nvgFillColor(vg, copy.error ? nvgRGB(245, 104, 104) : nvgRGB(0, 180, 216));
    nvgFill(vg);
}

void maskPassword(const char* src, char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    size_t len = src ? std::strlen(src) : 0;
    if (len >= outSize) len = outSize - 1;
    for (size_t i = 0; i < len; ++i) out[i] = '*';
    out[len] = '\0';
}

void drawConnectRow(NVGcontext* vg, int font, float y, const char* label, const char* value, bool focused) {
    const float x = 58.0f;
    const float w = 844.0f;
    const float h = 36.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y - 25.0f, w, h, 4.0f);
    nvgFillColor(vg, focused ? nvgRGB(36, 89, 116) : nvgRGB(20, 27, 34));
    nvgFill(vg);

    drawText(vg, font, x + 16.0f, y, 18.0f,
             focused ? nvgRGB(245, 250, 255) : nvgRGB(160, 174, 184), label);
    drawText(vg, font, x + 185.0f, y, 18.0f,
             focused ? nvgRGB(250, 253, 255) : nvgRGB(218, 226, 232),
             value && value[0] ? value : t("field.empty"));
}

void drawConnectActionButton(NVGcontext* vg, int font, float x, float y, float w,
                             const char* button, const char* label, bool focused) {
    const float h = 68.0f;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, x, y, w, h, 5.0f);
    nvgFillColor(vg, focused ? nvgRGB(38, 94, 122) : nvgRGB(20, 27, 34));
    nvgFill(vg);

    if (focused) {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, 4.0f);
        nvgStrokeWidth(vg, 1.5f);
        nvgStrokeColor(vg, nvgRGB(92, 186, 219));
        nvgStroke(vg);
    }

    nvgFontFaceId(vg, font);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
    nvgFontSize(vg, 15.0f);
    nvgFillColor(vg, focused ? nvgRGB(184, 235, 250) : nvgRGB(145, 164, 176));
    nvgText(vg, x + w * 0.5f, y + 25.0f, button, nullptr);

    nvgFontSize(vg, 19.0f);
    nvgFillColor(vg, focused ? nvgRGB(250, 253, 255) : nvgRGB(218, 226, 232));
    nvgText(vg, x + w * 0.5f, y + 51.0f, label, nullptr);
}

struct FooterHint {
    const char* key;
    const char* label;
};

void drawFooterHints(NVGcontext* vg, int font, const FooterHint* hints, int count) {
    float x = 40.0f;
    const float y = 512.0f;
    for (int i = 0; i < count; ++i) {
        if (!hints[i].key || !hints[i].label) continue;
        const float keyW = textWidth(vg, font, 14.0f, hints[i].key) + 20.0f;
        const float labelW = textWidth(vg, font, 16.0f, hints[i].label);
        const float itemW = keyW + labelW + 20.0f;
        if (x + itemW > 920.0f) break;

        nvgBeginPath(vg);
        nvgRoundedRect(vg, x, y, keyW, 22.0f, 4.0f);
        nvgFillColor(vg, nvgRGBA(255, 255, 255, 22));
        nvgFill(vg);
        nvgStrokeWidth(vg, 1.0f);
        nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 36));
        nvgStroke(vg);

        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 14.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(225, 234, 238));
        nvgText(vg, x + keyW * 0.5f, y + 11.0f, hints[i].key, nullptr);

        nvgFontSize(vg, 16.0f);
        nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, nvgRGB(178, 194, 204));
        nvgText(vg, x + keyW + 7.0f, y + 11.0f, hints[i].label, nullptr);
        x += itemW;
    }
}

void drawImageViewer(NVGcontext* vg, int font, const ImageState& image) {
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
    nvgFillColor(vg, nvgRGB(6, 8, 11));
    nvgFill(vg);

    if (image.loaded && image.nvgImage) {
        nvgSave(vg);
        nvgTranslate(vg, kWidth * 0.5f + image.offsetX, kHeight * 0.5f + image.offsetY);
        nvgRotate(vg, (static_cast<float>(image.rotationDegrees) * kPi) / 180.0f);
        const float w = static_cast<float>(image.width) * image.zoom;
        const float h = static_cast<float>(image.height) * image.zoom;
        NVGpaint img = nvgImagePattern(vg, -w * 0.5f, -h * 0.5f, w, h, 0.0f, image.nvgImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, -w * 0.5f, -h * 0.5f, w, h);
        nvgFillPaint(vg, img);
        nvgFill(vg);
        nvgRestore(vg);
    } else {
        nvgFontFaceId(vg, font);
        nvgFontSize(vg, 24.0f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgFillColor(vg, image.error ? nvgRGB(245, 104, 104) : nvgRGB(190, 202, 210));
        nvgText(vg, kWidth * 0.5f, kHeight * 0.5f - 16.0f,
                image.message[0] ? image.message : t("image.loading"), nullptr);
    }

    if (image.loaded && !image.hudVisible) return;

    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, 70.0f,
                                           nvgRGBA(0, 0, 0, 170), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, 70.0f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    drawMarqueeText(vg, font, 40.0f, 34.0f, 650.0f, 20.0f,
                    nvgRGB(245, 250, 255), image.fileName[0] ? image.fileName : t("app.title"), true);

    char info[96];
    if (image.loaded) {
        std::snprintf(info, sizeof(info), "%dx%d  %d%%",
                      image.width, image.height, static_cast<int>(image.zoom * 100.0f + 0.5f));
    } else {
        copyText(info, sizeof(info), image.error ? t("image.open_failed") : t("image.loading"));
    }
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGB(178, 194, 204));
    nvgText(vg, 920.0f, 34.0f, info, nullptr);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 500.0f, kWidth, 44.0f);
    nvgFillColor(vg, nvgRGB(10, 14, 18));
    nvgFill(vg);

    const FooterHint hints[] = {
        {"L/R", t("hint.prev_next")},
        {"左搖桿", t("hint.move")},
        {"↑↓/右搖桿", t("hint.zoom")},
        {"○", t("hint.reset")},
        {"△", t("hint.rotate")},
        {"□", t("hint.hud")},
        {"×", t("hint.back")},
    };
    drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
}

void formatTime(double seconds, char* out, size_t outSize) {
    if (seconds <= 0.0) {
        std::snprintf(out, outSize, "0:00");
        return;
    }
    const int total = static_cast<int>(seconds + 0.5);
    const int hours = total / 3600;
    const int minutes = (total / 60) % 60;
    const int secs = total % 60;
    if (hours > 0) std::snprintf(out, outSize, "%d:%02d:%02d", hours, minutes, secs);
    else std::snprintf(out, outSize, "%d:%02d", minutes, secs);
}

float clampFloat(float value, float lo, float hi) {
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

void drawSkipButton(NVGcontext* vg, int font, float cx, float cy, float r, bool forward, NVGcolor color) {
    nvgLineCap(vg, NVG_ROUND);
    nvgStrokeColor(vg, color);
    nvgStrokeWidth(vg, 3.0f);
    nvgBeginPath(vg);
    if (forward) nvgArc(vg, cx, cy, r, 1.5f * kPi, 0.0f, NVG_CCW);
    else nvgArc(vg, cx, cy, r, -0.5f * kPi, kPi, NVG_CW);
    nvgStroke(vg);

    const float ty = cy - r;
    nvgBeginPath(vg);
    if (forward) {
        nvgMoveTo(vg, cx - 6.0f, ty - 8.0f);
        nvgLineTo(vg, cx + 9.0f, ty);
        nvgLineTo(vg, cx - 6.0f, ty + 8.0f);
    } else {
        nvgMoveTo(vg, cx + 6.0f, ty - 8.0f);
        nvgLineTo(vg, cx - 9.0f, ty);
        nvgLineTo(vg, cx + 6.0f, ty + 8.0f);
    }
    nvgClosePath(vg);
    nvgFillColor(vg, color);
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 15.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, color);
    nvgText(vg, cx, cy + 2.0f, "10", nullptr);
}

void drawPlayPauseDisc(NVGcontext* vg, float cx, float cy, float r, bool paused) {
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    if (paused) {
        nvgMoveTo(vg, cx - 7.0f, cy - 12.0f);
        nvgLineTo(vg, cx + 13.0f, cy);
        nvgLineTo(vg, cx - 7.0f, cy + 12.0f);
        nvgClosePath(vg);
        nvgPathWinding(vg, NVG_HOLE);
    } else {
        nvgRect(vg, cx - 8.0f, cy - 11.0f, 5.5f, 22.0f);
        nvgPathWinding(vg, NVG_HOLE);
        nvgRect(vg, cx + 2.5f, cy - 11.0f, 5.5f, 22.0f);
        nvgPathWinding(vg, NVG_HOLE);
    }
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 210));
    nvgFill(vg);
}

void drawPlayerOverlay(NVGcontext* vg, int font, const PlayerState& player, float viewW, float viewH) {
    const NVGcolor white = nvgRGB(245, 245, 245);

    const NVGpaint top = nvgLinearGradient(vg, 0.0f, 0.0f, 0.0f, 58.0f,
                                           nvgRGBA(0, 0, 0, 160), nvgRGBA(0, 0, 0, 0));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, viewW, 58.0f);
    nvgFillPaint(vg, top);
    nvgFill(vg);

    const NVGpaint bottom = nvgLinearGradient(vg, 0.0f, viewH - 118.0f, 0.0f, viewH,
                                              nvgRGBA(0, 0, 0, 0), nvgRGBA(0, 0, 0, 180));
    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, viewH - 118.0f, viewW, 118.0f);
    nvgFillPaint(vg, bottom);
    nvgFill(vg);

    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 24.0f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, 16.0f, 23.0f, "←", nullptr);

    drawMarqueeText(vg, font, 48.0f, 29.0f, viewW - 285.0f, 18.0f,
                    white, player.fileName[0] ? player.fileName : t("app.title"), true);

    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 190));
    char rotateLine[40];
    std::snprintf(rotateLine, sizeof(rotateLine), t("player.rotate.fmt"), player.rotationDegrees == 270 ? 0 : 270);
    nvgText(vg, viewW - 118.0f, 23.0f, rotateLine, nullptr);
    nvgText(vg, viewW - 16.0f, 23.0f, t("player.back"), nullptr);

    const float transportY = viewH - 82.0f;
    const float playX = viewW - 44.0f;
    const float forwardX = viewW - 118.0f;
    const float backX = viewW - 192.0f;
    const NVGcolor button = nvgRGBA(255, 255, 255, 210);
    drawSkipButton(vg, font, backX, transportY, 17.0f, false, button);
    drawSkipButton(vg, font, forwardX, transportY, 17.0f, true, button);
    drawPlayPauseDisc(vg, playX, transportY, 26.0f, player.paused);

    char pos[16], dur[16], timeLine[40];
    formatTime(player.positionSeconds, pos, sizeof(pos));
    formatTime(player.durationSeconds, dur, sizeof(dur));
    std::snprintf(timeLine, sizeof(timeLine), "%s / %s", pos, dur);

    const float midY = viewH - 24.0f;
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 18.0f);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, white);
    nvgText(vg, 132.0f, midY, timeLine, nullptr);

    const float barX = 250.0f;
    const float barY = midY;
    const float barW = viewW - barX - 42.0f;
    float frac = 0.0f;
    if (player.durationSeconds > 0.0) {
        frac = static_cast<float>(player.positionSeconds / player.durationSeconds);
        frac = clampFloat(frac, 0.0f, 1.0f);
    }

    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - 3.0f, barW, 6.0f, 3.0f);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 60));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, barX, barY - 3.0f, barW * frac, 6.0f, 3.0f);
    nvgFillColor(vg, nvgRGB(0, 180, 216));
    nvgFill(vg);
    nvgBeginPath(vg);
    nvgCircle(vg, barX + barW * frac, barY, 7.0f);
    nvgFillColor(vg, nvgRGB(0, 180, 216));
    nvgFill(vg);
}

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

void renderUi(NVGcontext* vg, int font, const RuntimeStatus& runtime, const ScanState& scan,
              const PlayerState& player, const ImageState& image,
              AppMode mode, int selected, int listTop, int connectFocus) {
    if (mode == ModePlayer) {
        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
        nvgFillColor(vg, nvgRGB(0, 0, 0));
        nvgFill(vg);

        if (player.errorTitle[0]) {
            drawText(vg, font, 80.0f, 150.0f, 34.0f, nvgRGB(245, 104, 104), player.errorTitle);
            drawText(vg, font, 80.0f, 205.0f, 22.0f, nvgRGB(235, 238, 244), player.fileName);
            drawText(vg, font, 80.0f, 260.0f, 20.0f, nvgRGB(190, 202, 210), player.errorReason);
            drawText(vg, font, 80.0f, 340.0f, 18.0f, nvgRGB(150, 164, 172),
                     std::strcmp(player.errorTitle, t("error.resolution.title")) == 0
                         ? t("error.resolution.hint")
                         : t("error.convert.hint"));
            drawText(vg, font, 80.0f, 500.0f, 17.0f, nvgRGB(210, 218, 224), t("player.back"));
            return;
        }

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

        if (player.loading || player.paused || player.overlayFrames > 0 || !player.hasFrame) {
            if (player.rotationDegrees == 270) {
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

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, kHeight);
    nvgFillColor(vg, nvgRGB(12, 16, 21));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 0.0f, kWidth, 124.0f);
    nvgFillColor(vg, nvgRGB(22, 29, 36));
    nvgFill(vg);

    nvgBeginPath(vg);
    nvgRect(vg, 0.0f, 500.0f, kWidth, 44.0f);
    nvgFillColor(vg, nvgRGB(16, 21, 27));
    nvgFill(vg);

    if (mode == ModeConnect) {
        drawText(vg, font, 58.0f, 66.0f, 38.0f, nvgRGB(240, 245, 248), t("app.title"));
        drawText(vg, font, 60.0f, 108.0f, 20.0f, nvgRGB(170, 185, 194), t("login.title"));

        char passwordMasked[64];
        maskPassword(gConn.password, passwordMasked, sizeof(passwordMasked));
        drawConnectRow(vg, font, 158.0f, connectFieldLabel(ConnectServer), gConn.server, connectFocus == ConnectServer);
        drawConnectRow(vg, font, 194.0f, connectFieldLabel(ConnectShare), gConn.share, connectFocus == ConnectShare);
        drawConnectRow(vg, font, 230.0f, connectFieldLabel(ConnectPath), gConn.path, connectFocus == ConnectPath);
        drawConnectRow(vg, font, 266.0f, connectFieldLabel(ConnectUser), gConn.user, connectFocus == ConnectUser);
        drawConnectRow(vg, font, 302.0f, connectFieldLabel(ConnectPassword), passwordMasked, connectFocus == ConnectPassword);
        drawConnectRow(vg, font, 338.0f, connectFieldLabel(ConnectDomain), gConn.domain, connectFocus == ConnectDomain);
        drawConnectRow(vg, font, 374.0f, connectFieldLabel(ConnectLocalPath), gConn.localPath, connectFocus == ConnectLocalPath);
        drawConnectActionButton(vg, font, 58.0f, 408.0f, 268.0f,
                                "START", t("connect.smb_button"), connectFocus == ConnectSmbAction);
        drawConnectActionButton(vg, font, 346.0f, 408.0f, 268.0f,
                                "SELECT", t("connect.local_button"), connectFocus == ConnectLocalAction);
        drawConnectActionButton(vg, font, 634.0f, 408.0f, 268.0f,
                                "L", t("connect.hidden_button"), connectFocus == ConnectHiddenAction);

        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", t("hint.edit")},
            {"×", t("hint.exit")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    if (mode == ModeHidden) {
        drawText(vg, font, 40.0f, 58.0f, 34.0f, nvgRGB(240, 245, 248), t("hidden.title"));

        char countLine[128];
        std::snprintf(countLine, sizeof(countLine), t("hidden.count.fmt"), gHiddenItemCount);
        drawText(vg, font, 40.0f, 94.0f, 18.0f, nvgRGB(170, 185, 194), countLine);
        drawText(vg, font, 40.0f, 132.0f, 18.0f, nvgRGB(119, 220, 153), t("hidden.hint"));

        if (gHiddenItemCount > 0) {
            int first = listTop;
            if (first > gHiddenItemCount - kVisibleEntries) first = gHiddenItemCount - kVisibleEntries;
            if (first < 0) first = 0;
            const int last = (gHiddenItemCount - first) < kVisibleEntries
                ? gHiddenItemCount
                : first + kVisibleEntries;
            for (int i = first; i < last; ++i) {
                drawHiddenRow(vg, font, gHiddenItems[i], i - first, i == selected);
            }
        } else {
            drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("hidden.empty"));
        }

        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", t("hint.unhide")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
        return;
    }

    drawText(vg, font, 40.0f, 58.0f, 34.0f, nvgRGB(240, 245, 248), t("app.title"));

    char shareLine[256];
    if (scan.source == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        std::snprintf(shareLine, sizeof(shareLine), t("local.prefix.fmt"), scan.path[0] ? scan.path : localRoot);
    } else {
        std::snprintf(shareLine, sizeof(shareLine), t("share.prefix.fmt"), gConn.server, gConn.share,
                      scan.path[0] ? scan.path : "");
    }
    drawText(vg, font, 40.0f, 94.0f, 18.0f, nvgRGB(170, 185, 194), shareLine);

    char libLine[128];
    if (scan.source == SourceLocal) {
        std::snprintf(libLine, sizeof(libLine), t("runtime.local.fmt"), runtime.mpvClientApi);
    } else {
        std::snprintf(libLine, sizeof(libLine), t("runtime.smb.fmt"),
                      runtime.smb2ContextOk ? "OK" : t("runtime.failed"), runtime.mpvClientApi);
    }
    nvgFontFaceId(vg, font);
    nvgFontSize(vg, 13.0f);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE);
    nvgFillColor(vg, nvgRGB(92, 107, 118));
    nvgText(vg, 920.0f, 62.0f, libLine, nullptr);

    NVGcolor statusColor = nvgRGB(171, 185, 194);
    if (scan.phase == ScanReady) statusColor = nvgRGB(119, 220, 153);
    if (scan.phase == ScanError) statusColor = nvgRGB(245, 104, 104);
    drawText(vg, font, 40.0f, 132.0f, 18.0f, statusColor, scan.message);

    CopyState copy = {};
    lockScan();
    copy = gCopyState;
    unlockScan();
    drawCopyProgress(vg, font, copy);

    if (scan.phase == ScanReady && scan.count > 0) {
        int first = listTop;
        if (first > scan.count - kVisibleEntries) first = scan.count - kVisibleEntries;
        if (first < 0) first = 0;
        const int last = (scan.count - first) < kVisibleEntries ? scan.count : first + kVisibleEntries;
        for (int i = first; i < last; ++i) {
            drawEntryRow(vg, font, scan.entries[i], i - first, i == selected);
        }
    } else if (scan.phase == ScanReady) {
        drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("browser.empty"));
    } else if (scan.phase == ScanLoading) {
        drawText(vg, font, 56.0f, 220.0f, 23.0f, nvgRGB(210, 218, 224), t("browser.wait"));
    }

    const bool hasSelectedEntry = scan.phase == ScanReady && selected >= 0 && selected < scan.count;
    const bool selectedDir = hasSelectedEntry && scan.entries[selected].directory;
    const bool selectedImage = hasSelectedEntry && !selectedDir && isImageFile(scan.entries[selected].name);
    if (!hasSelectedEntry) {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else if (scan.source == SourceSmb && !selectedDir) {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", selectedImage ? t("hint.view") : t("hint.play")},
            {"□", t("hint.copy")},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    } else {
        const FooterHint hints[] = {
            {"↑↓", t("hint.select")},
            {"○", selectedDir ? t("hint.enter") : (selectedImage ? t("hint.view") : t("hint.play"))},
            {"SELECT", t("hint.hide")},
            {"L", t("hint.hidden")},
            {"△", t("hint.rescan")},
            {"×", t("hint.back")},
        };
        drawFooterHints(vg, font, hints, static_cast<int>(sizeof(hints) / sizeof(hints[0])));
    }
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

void zoomImage(float factor) {
    if (factor <= 0.0f) return;
    lockScan();
    const float fit = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    const float minZoom = fit * 0.25f;
    const float maxZoom = fit * 12.0f > 8.0f ? fit * 12.0f : 8.0f;
    gImage.zoom *= factor;
    if (gImage.zoom < minZoom) gImage.zoom = minZoom;
    if (gImage.zoom > maxZoom) gImage.zoom = maxZoom;
    clampImageViewLocked();
    unlockScan();
}

void panImage(float dx, float dy) {
    lockScan();
    gImage.offsetX += dx;
    gImage.offsetY += dy;
    clampImageViewLocked();
    unlockScan();
}

void rotateImageClockwise() {
    lockScan();
    gImage.rotationDegrees = (gImage.rotationDegrees + 90) % 360;
    gImage.zoom = fitImageScale(gImage.width, gImage.height, gImage.rotationDegrees);
    gImage.offsetX = 0.0f;
    gImage.offsetY = 0.0f;
    clampImageViewLocked();
    unlockScan();
}

void toggleImageHud() {
    lockScan();
    gImage.hudVisible = !gImage.hudVisible;
    unlockScan();
}

int findAdjacentImageIndex(const ScanState& scan, int selected, int direction) {
    if (scan.phase != ScanReady || scan.count <= 0 || direction == 0) return -1;
    int i = selected;
    for (int step = 0; step < scan.count; ++step) {
        i += direction;
        if (i < 0 || i >= scan.count) return -1;
        if (!scan.entries[i].directory && isImageFile(scan.entries[i].name)) return i;
    }
    return -1;
}

void openBrowserEntryAtIndex(const ScanState& snapshot, int index, int* selected, int* listTop,
                             AppMode* mode, NVGcontext* vg,
                             SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
                             SceGxmMultisampleMode msaa) {
    if (!selected || !listTop || !mode) return;
    if (snapshot.phase != ScanReady || index < 0 || index >= snapshot.count) return;

    *selected = index;
    keepSelectedVisible(*selected, snapshot.count, listTop);
    const SmbEntry& entry = snapshot.entries[index];
    if (entry.directory) {
        enterDirectory(entry.name, *selected, *listTop);
        *selected = 0;
        *listTop = 0;
        startScan();
    } else if (isImageFile(entry.name)) {
        openImageEntry(entry, snapshot.source, vg);
        *mode = ModeImage;
    } else {
        playEntry(entry, snapshot.source, gxmCtx, patcher, msaa, vg);
        *mode = ModePlayer;
    }
}

void keepSelectedVisible(int selected, int count, int* listTop) {
    if (!listTop) return;
    if (count <= kVisibleEntries) {
        *listTop = 0;
        return;
    }
    if (selected < *listTop) *listTop = selected;
    if (selected >= *listTop + kVisibleEntries) *listTop = selected - kVisibleEntries + 1;
    const int maxTop = count - kVisibleEntries;
    if (*listTop > maxTop) *listTop = maxTop;
    if (*listTop < 0) *listTop = 0;
}

void scrollListByRows(int rows, int count, int* selected, int* listTop) {
    if (!selected || !listTop || rows == 0 || count <= 0) return;
    const int maxTop = count > kVisibleEntries ? count - kVisibleEntries : 0;
    *listTop += rows;
    if (*listTop < 0) *listTop = 0;
    if (*listTop > maxTop) *listTop = maxTop;
    if (*selected < *listTop) *selected = *listTop;
    if (*selected >= *listTop + kVisibleEntries) *selected = *listTop + kVisibleEntries - 1;
    if (*selected >= count) *selected = count - 1;
    if (*selected < 0) *selected = 0;
}

int appMain(SceSize, void*) {
    sceClibPrintf("[vita-smb-player] start\n");

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
    sceClibPrintf("[vita-smb-player] libsmb2 context: %s\n", runtime.smb2ContextOk ? "ok" : "failed");
    sceClibPrintf("[vita-smb-player] mpv client api: %lu\n", runtime.mpvClientApi);

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
        sceClibPrintf("[vita-smb-player] gxmCreateWindow failed\n");
        if (networkOk) shutdownNetwork();
        return 1;
    }

    NVGcontext* vg = nvgCreateGXM(window->context, window->shader_patcher, NVG_STENCIL_STROKES);
    if (!vg) {
        sceClibPrintf("[vita-smb-player] nvgCreateGXM failed\n");
        gxmDeleteWindow(window);
        if (networkOk) shutdownNetwork();
        return 1;
    }

    int font = nvgCreateFont(vg, "sans", "app0:/CJK.ttf");
    if (font < 0) {
        sceClibPrintf("[vita-smb-player] CJK font failed, using Roboto\n");
        font = nvgCreateFont(vg, "sans", "app0:/Roboto-Regular.ttf");
        if (font < 0) {
            sceClibPrintf("[vita-smb-player] failed to load bundled font\n");
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
    AppMode mode = ModeConnect;
    AppMode hiddenReturnMode = ModeConnect;
    int hiddenReturnSelected = 0;
    int hiddenReturnListTop = 0;
    bool pendingBrowserFocusRestore = false;
    int pendingBrowserSelected = 0;
    int pendingBrowserListTop = 0;

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
        }

        pollMpvEvents();
        updateImeDialog();

        ScanState snapshot = {};
        PlayerState playerSnapshot = {};
        lockScan();
        snapshot = gScanState;
        playerSnapshot = gPlayer;
        unlockScan();

        bool consumedCross = false;
        const bool upAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_UP, &upHoldFrames);
        const bool downAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_DOWN, &downHoldFrames);
        const bool leftAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_LEFT, &leftHoldFrames);
        const bool rightAction = !gImeOpen && repeatButton(pad.buttons, previousPad.buttons, SCE_CTRL_RIGHT, &rightHoldFrames);
        const bool openHiddenPressed = !gImeOpen &&
            (mode == ModeConnect || mode == ModeBrowser) &&
            pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LTRIGGER);

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
            if (touching && gUiTouchPrev && !multiTouching) {
                gUiTouchScrollCarry += -(touchY - gUiTouchLastY);
                const int rows = static_cast<int>(gUiTouchScrollCarry / 38.0f);
                if (rows != 0) {
                    scrollListByRows(rows, gHiddenItemCount, &selected, &listTop);
                    gUiTouchScrollCarry -= rows * 38.0f;
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
                keepSelectedVisible(selected, snapshot.count, &listTop);
            }
            if (downAction && selected + 1 < snapshot.count) {
                ++selected;
                keepSelectedVisible(selected, snapshot.count, &listTop);
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
            if (touching && gUiTouchPrev && !multiTouching) {
                gUiTouchScrollCarry += -(touchY - gUiTouchLastY);
                const int rows = static_cast<int>(gUiTouchScrollCarry / 38.0f);
                if (rows != 0) {
                    scrollListByRows(rows, snapshot.count, &selected, &listTop);
                    gUiTouchScrollCarry -= rows * 38.0f;
                }
            }
            if (uiTouchEnded) {
                const float dx = gUiTouchLastX - gUiTouchStartX;
                const float dy = gUiTouchLastY - gUiTouchStartY;
                if (dx * dx + dy * dy < 24.0f * 24.0f) {
                    const int hit = listIndexAtPoint(gUiTouchLastX, gUiTouchLastY, listTop, snapshot.count);
                    if (hit >= 0) {
                        openBrowserEntryAtIndex(snapshot, hit, &selected, &listTop, &mode, vg,
                                                window->context, window->shader_patcher,
                                                static_cast<SceGxmMultisampleMode>(SCE_GXM_MULTISAMPLE_NONE));
                    }
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
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CIRCLE) &&
                !playerSnapshot.waitingForValidation) {
                showPlayerOverlay();
                setMpvPause(!gPlayer.paused);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_TRIANGLE)) {
                cycleMpvRotation();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_UP)) {
                showPlayerOverlay();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_DOWN)) {
                hidePlayerOverlay();
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_LEFT)) {
                showPlayerOverlay();
                seekMpvRelative(-10.0);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_RIGHT)) {
                showPlayerOverlay();
                seekMpvRelative(10.0);
            }
            if (pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
                stopCurrentPlayback();
                mode = ModeBrowser;
                consumedCross = true;
            }
            if (handlePlayerTouch(touching, touchX, touchY)) {
                stopCurrentPlayback();
                mode = ModeBrowser;
            }
        }

        if (mode != ModePlayer) {
            gTouchPrev = false;
            gTouchDraggingBar = false;
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
            if (pendingBrowserFocusRestore && snapshot.phase != ScanLoading) {
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
            quit = true;
        } else if (!gImeOpen && mode == ModeHidden && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS)) {
            mode = hiddenReturnMode;
            selected = hiddenReturnMode == ModeBrowser ? hiddenReturnSelected : 0;
            listTop = hiddenReturnMode == ModeBrowser ? hiddenReturnListTop : 0;
        } else if (!gImeOpen && mode == ModeBrowser && pressed(pad.buttons, previousPad.buttons, SCE_CTRL_CROSS) && !consumedCross) {
            if (goParentDirectory()) {
                if (popBrowserFocus(&pendingBrowserSelected, &pendingBrowserListTop)) {
                    pendingBrowserFocusRestore = true;
                } else {
                    selected = 0;
                    listTop = 0;
                    pendingBrowserFocusRestore = false;
                }
                startScan();
            } else {
                mode = ModeConnect;
            }
        }

        previousPad = pad;

        if (mode == ModePlayer && !gPlayer.paused) {
            tickPlayerOverlay();
            sceKernelPowerTick(SCE_KERNEL_POWER_TICK_DEFAULT);
        }
        renderMpvToFbo();

        gxmBeginFrame();
        gxmClear();

        nvgBeginFrame(vg, kWidth, kHeight, 1.0f);
        ImageState imageSnapshot = {};
        lockScan();
        imageSnapshot = gImage;
        unlockScan();
        renderUi(vg, font, runtime, snapshot, playerSnapshot, imageSnapshot,
                 mode, selected, listTop, connectFocus);
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
    } else {
        sceClibPrintf("[vita-smb-player] exiting while background work is still running\n");
    }
    sceAppUtilShutdown();

    sceClibPrintf("[vita-smb-player] exit\n");
    return 0;
}

} // namespace

int main() {
    SceUID thread = sceKernelCreateThread("vita_smb_player_main", appMain, 0x40, 8 * 1024 * 1024, 0, 0, nullptr);
    if (thread < 0) {
        sceClibPrintf("[vita-smb-player] sceKernelCreateThread failed: 0x%08x\n", thread);
        return 1;
    }

    sceKernelStartThread(thread, 0, nullptr);
    sceKernelWaitThreadEnd(thread, nullptr, nullptr);
    sceKernelExitProcess(0);
    return 0;
}
