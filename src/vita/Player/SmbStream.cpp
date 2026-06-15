#include "Player/SmbStream.hpp"

#include "Config/I18n.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Player/PlayerMessages.hpp"
#include "Utils/Text.hpp"

extern "C" {
#include <fcntl.h>
#include <mpv/client.h>
#include <mpv/stream_cb.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

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

} // namespace

int registerVitaSmbStream(mpv_handle* mpv) {
    return mpv_stream_cb_add_ro(mpv, "vitasmb", nullptr, smbStreamOpen);
}
