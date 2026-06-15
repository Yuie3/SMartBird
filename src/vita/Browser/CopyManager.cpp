#include "Browser/CopyManager.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/I18n.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Text.hpp"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>

extern "C" {
#include <fcntl.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

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

} // namespace

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
