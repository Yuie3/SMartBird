#pragma once

#include "nanovg.h"

namespace ui {

constexpr float kPageMargin = 32.0f;
constexpr float kPanelRadius = 8.0f;
constexpr float kTopBarHeight = 86.0f;
constexpr float kFooterTop = 502.0f;
constexpr float kFooterHeight = 42.0f;

NVGcolor textPrimary();
NVGcolor textSecondary();
NVGcolor textMuted();
NVGcolor accent();
NVGcolor accentSoft();
NVGcolor danger();

void drawAppBackground(NVGcontext* vg);
void drawTopBar(NVGcontext* vg, int font, const char* title, const char* subtitle);
void drawFooterBar(NVGcontext* vg);
void drawGlassPanel(NVGcontext* vg, float x, float y, float w, float h, float radius, bool strong);
void drawFocusPanel(NVGcontext* vg, float x, float y, float w, float h, float radius, float pulse = 0.0f);
void drawKeyCapsule(NVGcontext* vg, int font, float x, float y, const char* key, bool active);

} // namespace ui
