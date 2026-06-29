#include "Browser/BrowserState.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Utils/Text.hpp"

#include <cstdio>
#include <cstring>

namespace {

const char* normalizeSmbPath() {
    const char* path = optionalText(gConn.path);
    if (!path) return "";
    while (*path == '/') ++path;
    return path;
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

void clearBrowserForwardHistory() {
    gBrowserForwardHistoryCount = 0;
}

void parentPathOf(const char* path, char* out, size_t outSize) {
    copyText(out, outSize, path ? path : "");
    char* slash = std::strrchr(out, '/');
    if (slash) *slash = '\0';
    else out[0] = '\0';
    if (gBrowserSource == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        if (std::strncmp(out, localRoot, std::strlen(localRoot)) != 0) {
            copyText(out, outSize, localRoot);
        }
    }
}

bool currentPathAtRootLocked() {
    if (gBrowserSource == SourceLocal) {
        char localRoot[256];
        getConfiguredLocalRoot(localRoot, sizeof(localRoot));
        return std::strcmp(gCurrentPath, localRoot) == 0;
    }
    return gCurrentPath[0] == '\0';
}

void pushBrowserForward(const char* parentPath, const char* childPath, int selected, int listTop) {
    if (!parentPath || !childPath || !childPath[0]) return;
    if (gBrowserForwardHistoryCount >= kMaxBrowserHistory) {
        for (int i = 1; i < kMaxBrowserHistory; ++i) {
            gBrowserForwardHistory[i - 1] = gBrowserForwardHistory[i];
        }
        gBrowserForwardHistoryCount = kMaxBrowserHistory - 1;
    }
    BrowserForwardEntry& entry = gBrowserForwardHistory[gBrowserForwardHistoryCount++];
    copyText(entry.parentPath, sizeof(entry.parentPath), parentPath);
    copyText(entry.childPath, sizeof(entry.childPath), childPath);
    entry.selected = selected;
    entry.listTop = listTop;
}

} // namespace

const char* smbPathForDisplay() {
    const char* path = optionalText(gConn.path);
    return path ? path : "";
}

void initCurrentPath() {
    const char* initial = normalizeSmbPath();
    copyText(gCurrentPath, sizeof(gCurrentPath), initial);
    gBrowserHistoryCount = 0;
    clearBrowserForwardHistory();
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
    clearBrowserForwardHistory();
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
    clearBrowserForwardHistory();
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

bool goParentDirectoryAndRememberForward(int selected, int listTop) {
    lockScan();
    if (currentPathAtRootLocked()) {
        unlockScan();
        return false;
    }
    char childPath[256];
    char parentPath[256];
    copyText(childPath, sizeof(childPath), gCurrentPath);
    parentPathOf(childPath, parentPath, sizeof(parentPath));
    pushBrowserForward(parentPath, childPath, selected, listTop);
    copyText(gCurrentPath, sizeof(gCurrentPath), parentPath);
    unlockScan();
    return true;
}

bool goForwardDirectory(int* selected, int* listTop) {
    lockScan();
    if (gBrowserForwardHistoryCount <= 0) {
        unlockScan();
        return false;
    }
    const BrowserForwardEntry& entry = gBrowserForwardHistory[gBrowserForwardHistoryCount - 1];
    if (std::strcmp(entry.parentPath, gCurrentPath) != 0) {
        clearBrowserForwardHistory();
        unlockScan();
        return false;
    }
    copyText(gCurrentPath, sizeof(gCurrentPath), entry.childPath);
    if (selected) *selected = entry.selected;
    if (listTop) *listTop = entry.listTop;
    --gBrowserForwardHistoryCount;
    unlockScan();
    return true;
}

bool hasForwardDirectory() {
    lockScan();
    const bool hasForward = gBrowserForwardHistoryCount > 0 &&
        std::strcmp(gBrowserForwardHistory[gBrowserForwardHistoryCount - 1].parentPath, gCurrentPath) == 0;
    unlockScan();
    return hasForward;
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
