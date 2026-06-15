#pragma once

#include "Core/Types.hpp"
#include "nanovg.h"

struct FooterHint {
    const char* key;
    const char* label;
};

void drawText(NVGcontext* vg, int font, float x, float y, float size, NVGcolor color, const char* text);
float textWidth(NVGcontext* vg, int font, float size, const char* text);
void drawMarqueeText(NVGcontext* vg, int font, float x, float y, float maxW, float size,
                     NVGcolor color, const char* text, bool active);
void drawFolderGlyph(NVGcontext* vg, float x, float y, NVGcolor color);
void drawPlayGlyph(NVGcontext* vg, float x, float y, NVGcolor color);
void drawPhotoGlyph(NVGcontext* vg, float x, float y, NVGcolor color);
void drawSmallBadge(NVGcontext* vg, int font, float x, float y, float w,
                    const char* label, NVGcolor bg, NVGcolor fg);
void drawEntryRow(NVGcontext* vg, int font, const SmbEntry& entry, int row, bool selected);
void drawHiddenRow(NVGcontext* vg, int font, const HiddenItem& item, int row, bool selected);
void drawCopyProgress(NVGcontext* vg, int font, const CopyState& copy);
void drawConnectRow(NVGcontext* vg, int font, float y, const char* label, const char* value, bool focused);
void drawConnectActionButton(NVGcontext* vg, int font, float x, float y, float w,
                             const char* button, const char* label, bool focused);
void drawFooterHints(NVGcontext* vg, int font, const FooterHint* hints, int count);
