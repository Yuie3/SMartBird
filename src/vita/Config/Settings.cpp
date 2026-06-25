#include "Config/Settings.hpp"

#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "SmbConfig.hpp"
#include "Utils/Text.hpp"

#include <cstdio>
#include <cstring>

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

void initConnectionDefaults() {
    copyText(gConn.server, sizeof(gConn.server), VITA_SMB_SERVER);
    copyText(gConn.share, sizeof(gConn.share), VITA_SMB_SHARE);
    copyText(gConn.path, sizeof(gConn.path), VITA_SMB_PATH);
    copyText(gConn.user, sizeof(gConn.user), VITA_SMB_USER);
    copyText(gConn.password, sizeof(gConn.password), VITA_SMB_PASSWORD);
    copyText(gConn.domain, sizeof(gConn.domain), VITA_SMB_DOMAIN);
    copyText(gConn.localPath, sizeof(gConn.localPath), kDefaultLocalRoot);
}

namespace {

constexpr const char* kConnectionConfigPath = "ux0:/data/Smbird/connection.txt";

void applyConfigLine(const char* key, const char* value) {
    if (std::strcmp(key, "server") == 0) copyText(gConn.server, sizeof(gConn.server), value);
    else if (std::strcmp(key, "share") == 0) copyText(gConn.share, sizeof(gConn.share), value);
    else if (std::strcmp(key, "path") == 0) copyText(gConn.path, sizeof(gConn.path), value);
    else if (std::strcmp(key, "user") == 0) copyText(gConn.user, sizeof(gConn.user), value);
    else if (std::strcmp(key, "password") == 0) copyText(gConn.password, sizeof(gConn.password), value);
    else if (std::strcmp(key, "domain") == 0) copyText(gConn.domain, sizeof(gConn.domain), value);
    else if (std::strcmp(key, "local_path") == 0) copyText(gConn.localPath, sizeof(gConn.localPath), value);
}

} // namespace

void loadConnectionConfig() {
    FILE* fp = std::fopen(kConnectionConfigPath, "r");
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
    FILE* fp = std::fopen(kConnectionConfigPath, "w");
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
