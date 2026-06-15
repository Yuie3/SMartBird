#include "Utils/Text.hpp"

#include <cstdio>
#include <cstring>

void copyText(char* dst, size_t dstSize, const char* src) {
    if (dstSize == 0) return;
    if (!src) src = "";
    std::snprintf(dst, dstSize, "%s", src);
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

const char* optionalText(const char* value) {
    return value && value[0] ? value : nullptr;
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
