#include "UI/ImeInput.hpp"

#include "Config/I18n.hpp"
#include "Config/Settings.hpp"
#include "Core/Constants.hpp"
#include "Core/State.hpp"
#include "Core/Types.hpp"
#include "Utils/Text.hpp"

#include <psp2/common_dialog.h>
#include <psp2/ime_dialog.h>

#include <cstdio>
#include <cstring>

namespace {

void utf8ToUtf16(const char* src, SceWChar16* dst, size_t dstCount) {
    if (!dst || dstCount == 0) return;
    size_t out = 0;
    const unsigned char* s = reinterpret_cast<const unsigned char*>(src ? src : "");
    while (*s && out + 1 < dstCount) {
        unsigned int cp = 0;
        if (*s < 0x80) {
            cp = *s++;
        } else if ((*s & 0xE0) == 0xC0 && s[1]) {
            cp = ((*s & 0x1F) << 6) | (s[1] & 0x3F);
            s += 2;
        } else if ((*s & 0xF0) == 0xE0 && s[1] && s[2]) {
            cp = ((*s & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
            s += 3;
        } else {
            ++s;
            continue;
        }
        dst[out++] = static_cast<SceWChar16>(cp <= 0xFFFF ? cp : '?');
    }
    dst[out] = 0;
}

void utf16ToUtf8(const SceWChar16* src, char* dst, size_t dstSize) {
    if (!dst || dstSize == 0) return;
    size_t out = 0;
    for (; src && *src && out + 1 < dstSize; ++src) {
        const unsigned int cp = *src;
        if (cp < 0x80) {
            dst[out++] = static_cast<char>(cp);
        } else if (cp < 0x800 && out + 2 < dstSize) {
            dst[out++] = static_cast<char>(0xC0 | (cp >> 6));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (out + 3 < dstSize) {
            dst[out++] = static_cast<char>(0xE0 | (cp >> 12));
            dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            break;
        }
    }
    dst[out] = '\0';
}

} // namespace

const char* connectFieldLabel(int field) {
    switch (field) {
    case ConnectServer: return t("field.server");
    case ConnectShare: return t("field.share");
    case ConnectPath: return t("field.path");
    case ConnectUser: return t("field.user");
    case ConnectPassword: return t("field.password");
    case ConnectDomain: return t("field.domain");
    case ConnectLocalPath: return t("field.local_path");
    default: return "";
    }
}

void openImeForField(int field) {
    char* value = connectFieldValue(field);
    const size_t valueSize = connectFieldSize(field);
    if (!value || valueSize == 0 || gImeOpen) return;

    std::memset(gImeBuffer, 0, sizeof(gImeBuffer));
    utf8ToUtf16(value, gImeBuffer, sizeof(gImeBuffer) / sizeof(gImeBuffer[0]));

    std::memset(gImeTitle, 0, sizeof(gImeTitle));
    char titleText[96];
    std::snprintf(titleText, sizeof(titleText), t("ime.edit.fmt"), connectFieldLabel(field));
    utf8ToUtf16(titleText, gImeTitle, sizeof(gImeTitle) / sizeof(gImeTitle[0]));

    SceImeDialogParam param;
    sceImeDialogParamInit(&param);
    param.supportedLanguages = SCE_IME_LANGUAGE_TRADITIONAL_CHINESE
                             | SCE_IME_LANGUAGE_SIMPLIFIED_CHINESE
                             | SCE_IME_LANGUAGE_JAPANESE
                             | SCE_IME_LANGUAGE_ENGLISH;
    param.type = SCE_IME_TYPE_DEFAULT;
    param.dialogMode = SCE_IME_DIALOG_DIALOG_MODE_WITH_CANCEL;
    param.textBoxMode = field == ConnectPassword
        ? SCE_IME_DIALOG_TEXTBOX_MODE_PASSWORD
        : SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
    param.title = gImeTitle;
    param.maxTextLength = static_cast<SceUInt32>(valueSize > 1 ? valueSize - 1 : 0);
    param.inputTextBuffer = gImeBuffer;
    param.initialText = gImeBuffer;

    if (sceImeDialogInit(&param) >= 0) {
        gImeOpen = true;
        gImeField = field;
    }
}

void updateImeDialog() {
    if (!gImeOpen) return;
    if (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;

    SceImeDialogResult result = {};
    sceImeDialogGetResult(&result);
    sceImeDialogTerm();

    if (result.button == SCE_IME_DIALOG_BUTTON_ENTER) {
        char* value = connectFieldValue(gImeField);
        const size_t valueSize = connectFieldSize(gImeField);
        if (value && valueSize > 0) {
            utf16ToUtf8(gImeBuffer, value, valueSize);
            if (gImeField == ConnectLocalPath && !value[0]) {
                copyText(value, valueSize, kDefaultLocalRoot);
            }
            saveConnectionConfig();
        }
    }

    gImeOpen = false;
    gImeField = -1;
}
