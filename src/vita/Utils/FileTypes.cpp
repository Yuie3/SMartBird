#include "Utils/FileTypes.hpp"

#include "Utils/Text.hpp"

#include <cstring>

bool isHiddenOrSystemName(const char* name) {
    if (!name || !name[0]) return true;
    if (name[0] == '.') return true;
    if (equalsNoCase(name, "$RECYCLE.BIN")) return true;
    if (equalsNoCase(name, "System Volume Information")) return true;
    if (equalsNoCase(name, "desktop.ini")) return true;
    if (equalsNoCase(name, "Thumbs.db")) return true;
    if (equalsNoCase(name, "ehthumbs.db")) return true;
    if (equalsNoCase(name, "@eaDir")) return true;
    if (equalsNoCase(name, "__MACOSX")) return true;
    if (equalsNoCase(name, "lost+found")) return true;
    if (std::strncmp(name, "._", 2) == 0) return true;
    return false;
}

bool isVideoFile(const char* name) {
    return endsWithNoCase(name, ".mp4") ||
           endsWithNoCase(name, ".m4v") ||
           endsWithNoCase(name, ".mkv") ||
           endsWithNoCase(name, ".avi") ||
           endsWithNoCase(name, ".mov") ||
           endsWithNoCase(name, ".webm") ||
           endsWithNoCase(name, ".ts");
}

bool isImageFile(const char* name) {
    return endsWithNoCase(name, ".jpg") ||
           endsWithNoCase(name, ".jpeg") ||
           endsWithNoCase(name, ".png") ||
           endsWithNoCase(name, ".bmp") ||
           endsWithNoCase(name, ".gif") ||
           endsWithNoCase(name, ".tga");
}

bool isMediaFile(const char* name) {
    return isVideoFile(name) || isImageFile(name);
}

const char* fileExtensionLabel(const char* name) {
    const char* dot = nullptr;
    for (const char* p = name ? name : ""; *p; ++p) {
        if (*p == '.') dot = p;
    }
    if (!dot || !dot[1]) return "VID";
    if (equalsNoCase(dot + 1, "mp4")) return "MP4";
    if (equalsNoCase(dot + 1, "m4v")) return "M4V";
    if (equalsNoCase(dot + 1, "mkv")) return "MKV";
    if (equalsNoCase(dot + 1, "avi")) return "AVI";
    if (equalsNoCase(dot + 1, "mov")) return "MOV";
    if (equalsNoCase(dot + 1, "webm")) return "WEBM";
    if (equalsNoCase(dot + 1, "ts")) return "TS";
    return "VID";
}
