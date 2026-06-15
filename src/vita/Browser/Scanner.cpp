#include "Browser/Scanner.hpp"

#include "Browser/BrowserState.hpp"
#include "Config/HiddenItems.hpp"
#include "Config/I18n.hpp"
#include "Config/Settings.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Utils/FileTypes.hpp"
#include "Utils/Text.hpp"

#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>

extern "C" {
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
}

#include <cstdio>
#include <cstring>

namespace {

void resetScanEntries(const char* message) {
    lockScan();
    gScanState.phase = ScanLoading;
    gScanState.source = gBrowserSource;
    gScanState.count = 0;
    copyText(gScanState.path, sizeof(gScanState.path), gCurrentPath);
    copyText(gScanState.message, sizeof(gScanState.message), message);
    unlockScan();
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

} // namespace

void setScanMessage(int phase, const char* message) {
    lockScan();
    gScanState.phase = phase;
    copyText(gScanState.message, sizeof(gScanState.message), message);
    unlockScan();
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
