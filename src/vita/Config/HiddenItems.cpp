#include "Config/HiddenItems.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Utils/Json.hpp"
#include "Utils/Text.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

constexpr const char* kHiddenItemsPath = "ux0:/data/Smbird/hidden.json";

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
    FILE* fp = std::fopen(kHiddenItemsPath, "w");
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

} // namespace

void loadHiddenItems() {
    gHiddenItemCount = 0;
    FILE* fp = std::fopen(kHiddenItemsPath, "r");
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
