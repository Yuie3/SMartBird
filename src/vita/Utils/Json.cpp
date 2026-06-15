#include "Utils/Json.hpp"

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

namespace {

void skipJsonObject(const char** cursor);
void skipJsonArray(const char** cursor);

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

} // namespace

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
