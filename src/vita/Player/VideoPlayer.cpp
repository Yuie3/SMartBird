#include "Player/VideoPlayer.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/I18n.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Player/PlayerMessages.hpp"
#include "Player/SmbStream.hpp"
#include "Utils/Text.hpp"
#include "nanovg.h"
#include "nanovg_gxm.h"

extern "C" {
#include <mpv/client.h>
#include <mpv/render.h>
#include <mpv/render_gxm.h>
#include <psp2/motion.h>
}

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int gAutoRotateCandidateDegrees = -1;
int gAutoRotateCandidateFrames = 0;
bool gMotionSamplingStarted = false;

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

int detectMotionRotationDegrees(const SceMotionState& motion) {
    float x = motion.basicOrientation.x;
    float y = motion.basicOrientation.y;
    if (std::fabs(x) < 0.5f && std::fabs(y) < 0.5f) {
        x = motion.acceleration.x;
        y = motion.acceleration.y;
    }
    const float absX = std::fabs(x);
    const float absY = std::fabs(y);
    constexpr float kMinimumTilt = 0.24f;
    constexpr float kDominanceRatio = 1.25f;

    if (absX < kMinimumTilt && absY < kMinimumTilt) return -1;
    if (absX > absY * kDominanceRatio) return x > 0.0f ? 90 : 270;
    if (absY > absX * kDominanceRatio) return y > 0.0f ? 0 : 180;
    return -1;
}

void resetAutoRotateDebounce() {
    gAutoRotateCandidateDegrees = -1;
    gAutoRotateCandidateFrames = 0;
}

bool startMotionSampling() {
    if (gMotionSamplingStarted) return true;
    const int rc = sceMotionStartSampling();
    if (rc < 0) return false;
    sceMotionSetDeadband(1);
    sceMotionSetTiltCorrection(1);
    gMotionSamplingStarted = true;
    return true;
}

void stopMotionSampling() {
    if (!gMotionSamplingStarted) return;
    sceMotionStopSampling();
    gMotionSamplingStarted = false;
    resetAutoRotateDebounce();
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
    const double speed = getMpvDouble("speed");
    const int paused = getMpvFlag("pause");
    int width = getMpvIntProperty("video-params/w");
    int height = getMpvIntProperty("video-params/h");
    if (width <= 0) width = getMpvIntProperty("width");
    if (height <= 0) height = getMpvIntProperty("height");

    lockScan();
    if (pos >= 0.0 && pos < 24.0 * 60.0 * 60.0) gPlayer.positionSeconds = pos;
    if (dur > 0.0 && dur < 24.0 * 60.0 * 60.0) gPlayer.durationSeconds = dur;
    if (speed >= 0.25 && speed <= 4.0) gPlayer.speed = speed;
    gPlayer.paused = paused;
    unlockScan();

    if (width > 0 && height > 0) applyDetectedVideoDimensions(width, height);
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

} // namespace

void showPlayerOverlay() {
    lockScan();
    gPlayer.hudVisible = 1;
    gPlayer.overlayFrames = 240;
    if (gPlayer.hudAnim < 0.02f) gPlayer.hudAnim = 0.02f;
    unlockScan();
}

void hidePlayerOverlay() {
    lockScan();
    gPlayer.hudVisible = 0;
    gPlayer.speedSliderVisible = 0;
    gPlayer.settingsVisible = 0;
    gPlayer.overlayFrames = 0;
    unlockScan();
}

void togglePlayerOverlay() {
    lockScan();
    const bool visible = gPlayer.hudVisible &&
        (gPlayer.loading || gPlayer.paused || gPlayer.overlayFrames > 0 ||
         gPlayer.speedSliderVisible || gPlayer.settingsVisible ||
         !gPlayer.hasFrame || gPlayer.hudAnim > 0.35f);
    gPlayer.hudVisible = visible ? 0 : 1;
    gPlayer.speedSliderVisible = 0;
    gPlayer.settingsVisible = 0;
    gPlayer.overlayFrames = visible ? 0 : 240;
    if (!visible && gPlayer.hudAnim < 0.02f) gPlayer.hudAnim = 0.02f;
    unlockScan();
}

void tickPlayerOverlay() {
    lockScan();
    if (gPlayer.overlayFrames > 0) {
        --gPlayer.overlayFrames;
        if (gPlayer.overlayFrames == 0 && !gPlayer.paused && !gPlayer.loading &&
            !gPlayer.speedSliderVisible && !gPlayer.settingsVisible && gPlayer.hasFrame) {
            gPlayer.hudVisible = 0;
        }
    }

    const float target = gPlayer.hudVisible ? 1.0f : 0.0f;
    const float step = gPlayer.hudVisible ? 0.085f : 0.075f;
    if (gPlayer.hudAnim < target) {
        gPlayer.hudAnim += step;
        if (gPlayer.hudAnim > target) gPlayer.hudAnim = target;
    } else if (gPlayer.hudAnim > target) {
        gPlayer.hudAnim -= step;
        if (gPlayer.hudAnim < target) gPlayer.hudAnim = target;
    }
    unlockScan();
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

void setMpvSpeed(double speed) {
    if (!gPlayer.mpv) return;
    if (speed < 0.5) speed = 0.5;
    if (speed > 2.0) speed = 2.0;
    mpv_set_property(gPlayer.mpv, "speed", MPV_FORMAT_DOUBLE, &speed);
    lockScan();
    gPlayer.speed = speed;
    std::snprintf(gPlayer.message, sizeof(gPlayer.message), t("player.speed.fmt"), speed);
    unlockScan();
}

void togglePlayerSpeedSlider() {
    lockScan();
    gPlayer.hudVisible = 1;
    gPlayer.overlayFrames = 240;
    if (gPlayer.hudAnim < 0.02f) gPlayer.hudAnim = 0.02f;
    const int nextVisible = !gPlayer.speedSliderVisible;
    gPlayer.speedSliderVisible = nextVisible;
    if (nextVisible) gPlayer.settingsVisible = 0;
    unlockScan();
}

void togglePlayerSettingsPanel() {
    lockScan();
    gPlayer.hudVisible = 1;
    gPlayer.overlayFrames = 240;
    if (gPlayer.hudAnim < 0.02f) gPlayer.hudAnim = 0.02f;
    const int nextVisible = !gPlayer.settingsVisible;
    gPlayer.settingsVisible = nextVisible;
    if (nextVisible) gPlayer.speedSliderVisible = 0;
    unlockScan();
}

void adjustMpvSpeed(int direction) {
    if (!gPlayer.mpv || direction == 0) return;
    constexpr double speeds[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    constexpr int speedCount = static_cast<int>(sizeof(speeds) / sizeof(speeds[0]));

    double current = 1.0;
    lockScan();
    current = gPlayer.speed > 0.0 ? gPlayer.speed : 1.0;
    unlockScan();

    int index = 2;
    double bestDiff = 999.0;
    for (int i = 0; i < speedCount; ++i) {
        const double diff = speeds[i] > current ? speeds[i] - current : current - speeds[i];
        if (diff < bestDiff) {
            bestDiff = diff;
            index = i;
        }
    }
    index += direction > 0 ? 1 : -1;
    if (index < 0) index = 0;
    if (index >= speedCount) index = speedCount - 1;

    setMpvSpeed(speeds[index]);
    showPlayerOverlay();
}

void toggleMpvLoop() {
    lockScan();
    const int enabled = gPlayer.loopPlayback ? 0 : 1;
    gPlayer.loopPlayback = enabled;
    copyText(gPlayer.message, sizeof(gPlayer.message), enabled ? t("player.loop_on") : t("player.loop_off"));
    unlockScan();

    if (gPlayer.mpv) {
        mpv_set_property_string(gPlayer.mpv, "loop-file", enabled ? "inf" : "no");
    }
    showPlayerOverlay();
}

void toggleMpvAutoRotate() {
    int enabled = 0;
    lockScan();
    enabled = gPlayer.autoRotateEnabled ? 0 : 1;
    gPlayer.autoRotateEnabled = enabled;
    copyText(gPlayer.message, sizeof(gPlayer.message),
             enabled ? t("player.auto_rotate_on") : t("player.auto_rotate_off"));
    unlockScan();

    resetAutoRotateDebounce();
    if (enabled && !startMotionSampling()) {
        lockScan();
        gPlayer.autoRotateEnabled = 0;
        copyText(gPlayer.message, sizeof(gPlayer.message), t("player.auto_rotate_unavailable"));
        unlockScan();
    } else if (!enabled) {
        stopMotionSampling();
        setMpvRotation(0);
    } else {
        updateMpvAutoRotation();
    }
    showPlayerOverlay();
}

void updateMpvAutoRotation() {
    int enabled = 0;
    int current = 0;
    lockScan();
    enabled = gPlayer.autoRotateEnabled;
    current = gPlayer.rotationDegrees;
    unlockScan();
    if (!enabled) return;
    if (!startMotionSampling()) return;

    SceMotionState motion = {};
    if (sceMotionGetState(&motion) < 0) {
        resetAutoRotateDebounce();
        return;
    }

    const int target = detectMotionRotationDegrees(motion);
    if (target < 0 || target == current) {
        resetAutoRotateDebounce();
        return;
    }

    if (gAutoRotateCandidateDegrees != target) {
        gAutoRotateCandidateDegrees = target;
        gAutoRotateCandidateFrames = 1;
        return;
    }

    ++gAutoRotateCandidateFrames;
    if (gAutoRotateCandidateFrames >= 24) {
        setMpvRotation(target);
        resetAutoRotateDebounce();
    }
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
    gPlayer.hudVisible = 1;
    gPlayer.speedSliderVisible = 0;
    gPlayer.settingsVisible = 0;
    gPlayer.swipeSeeking = 0;
    gPlayer.hudAnim = 1.0f;
    gPlayer.speed = 1.0;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.swipeSeekStartSeconds = 0.0;
    gPlayer.swipeSeekOffsetSeconds = 0.0;
    gPlayer.swipeSeekTargetSeconds = 0.0;
    gPlayer.overlayFrames = 0;
    gPlayer.waitingForValidation = 0;
    copyText(gPlayer.message, sizeof(gPlayer.message), t("player.stopped_short"));
    unlockScan();
    stopMotionSampling();
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
    mpv_set_option_string(gPlayer.mpv, "video-timing-offset", "0");
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

    int rc = registerVitaSmbStream(gPlayer.mpv);
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
    gPlayer.hudVisible = 1;
    gPlayer.speedSliderVisible = 0;
    gPlayer.settingsVisible = 0;
    gPlayer.swipeSeeking = 0;
    gPlayer.autoRotateEnabled = 0;
    gPlayer.hudAnim = 0.0f;
    gPlayer.loopPlayback = 0;
    gPlayer.speed = 1.0;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.swipeSeekStartSeconds = 0.0;
    gPlayer.swipeSeekOffsetSeconds = 0.0;
    gPlayer.swipeSeekTargetSeconds = 0.0;
    gPlayer.videoWidth = 0;
    gPlayer.videoHeight = 0;
    gPlayer.rotationDegrees = 0;
    gPlayer.waitingForValidation = 0;
    stopMotionSampling();
}

void renderMpvToFbo(bool forceRedraw) {
    if (!gPlayer.renderCtx) return;
    const uint64_t updateFlags = mpv_render_context_update(gPlayer.renderCtx);
    const bool hasNewFrame = (updateFlags & MPV_RENDER_UPDATE_FRAME) != 0;
    if (!forceRedraw && !hasNewFrame) return;

    int blockForTargetTime = 0;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_FLIP_Y, &gPlayer.flipY},
        {MPV_RENDER_PARAM_GXM_FBO, &gPlayer.mpvFbo},
        {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    const int rc = mpv_render_context_render(gPlayer.renderCtx, params);
    if (rc >= 0) {
        mpv_render_context_report_swap(gPlayer.renderCtx);
        if (hasNewFrame || gPlayer.hasFrame) gPlayer.hasFrame = 1;
    }
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
    resetAutoRotateDebounce();
    double speed = 1.0;
    mpv_set_property(gPlayer.mpv, "speed", MPV_FORMAT_DOUBLE, &speed);
    int loopEnabled = 0;
    int autoRotateEnabled = 0;
    lockScan();
    loopEnabled = gPlayer.loopPlayback;
    autoRotateEnabled = gPlayer.autoRotateEnabled;
    unlockScan();
    mpv_set_property_string(gPlayer.mpv, "loop-file", loopEnabled ? "inf" : "no");
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
    gPlayer.hudVisible = 1;
    gPlayer.speedSliderVisible = 0;
    gPlayer.settingsVisible = 0;
    gPlayer.swipeSeeking = 0;
    gPlayer.autoRotateEnabled = autoRotateEnabled;
    gPlayer.loopPlayback = loopEnabled;
    gPlayer.hudAnim = 1.0f;
    gPlayer.speed = 1.0;
    gPlayer.positionSeconds = 0.0;
    gPlayer.durationSeconds = 0.0;
    gPlayer.swipeSeekStartSeconds = 0.0;
    gPlayer.swipeSeekOffsetSeconds = 0.0;
    gPlayer.swipeSeekTargetSeconds = 0.0;
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
            if (end && end->reason == MPV_END_FILE_REASON_EOF && !gPlayer.loopPlayback) {
                gPlayer.hudVisible = 1;
                gPlayer.overlayFrames = 240;
                if (gPlayer.hudAnim < 0.02f) gPlayer.hudAnim = 0.02f;
            }
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
