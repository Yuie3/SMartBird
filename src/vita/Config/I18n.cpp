#include "Config/I18n.hpp"

#include "Utils/Json.hpp"
#include "Utils/Text.hpp"

#include <psp2/appmgr.h>
#include <psp2/apputil.h>
#include <psp2/system_param.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

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
    {"exit.title", "退出", "Exit"},
    {"exit.message", "Vita3K 目前在結束自製程式時可能會閃退。", "Vita3K may currently crash when this homebrew app exits."},
    {"exit.detail", "請直接關閉 Vita3K 視窗，或按 ○ 回到啟動頁。", "Close the Vita3K window directly, or press O to return to the start page."},
    {"exit.vita3k", "這不會中斷 SMB 或播放流程；只是避免走到模擬器崩潰路徑。", "This keeps the app alive to avoid the emulator crash path."},
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
    {"player.orientation.portrait", "直向", "Portrait"},
    {"player.orientation.landscape", "橫向", "Landscape"},
    {"player.speed.fmt", "%.2gx", "%.2gx"},
    {"player.speed_hint", "倍速", "Speed"},
    {"player.settings", "設定", "Settings"},
    {"player.settings.title", "播放設定", "Playback Settings"},
    {"player.loop", "循環", "Loop"},
    {"player.loop_playback", "循環播放", "Loop playback"},
    {"player.rotation", "畫面方向", "Orientation"},
    {"player.auto_rotate", "自動旋轉", "Auto rotate"},
    {"player.auto_rotate_hint", "依照 Vita 方向切換", "Follow Vita orientation"},
    {"player.auto_rotate_on", "自動旋轉開啟", "Auto rotate on"},
    {"player.auto_rotate_off", "自動旋轉關閉", "Auto rotate off"},
    {"player.auto_rotate_unavailable", "自動旋轉不可用", "Auto rotate unavailable"},
    {"player.loop_on", "循環播放", "Loop on"},
    {"player.loop_off", "取消循環", "Loop off"},
    {"player.swipe_seek.title", "快速跳轉", "Swipe Seek"},
    {"player.swipe_seek.seconds.fmt", "%+d 秒", "%+d sec"},
    {"player.swipe_seek.time.fmt", "%s → %s", "%s → %s"},
    {"player.controls", "○ 播放/暫停   ←/→ 10 秒   △ 旋轉   ↑ 顯示   ↓ 隱藏",
                        "O Play/Pause   LEFT/RIGHT 10s   TRIANGLE Rotate   UP Show   DOWN Hide"},
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

void applyTranslationOverride(const char* key, const char* value) {
    if (!key || !value) return;
    for (int i = 0; i < kTranslationCount; ++i) {
        if (std::strcmp(kTranslations[i].key, key) == 0) {
            copyText(gTranslationOverrides[i], sizeof(gTranslationOverrides[i]), value);
            return;
        }
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

} // namespace

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
