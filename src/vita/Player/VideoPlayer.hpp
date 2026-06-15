#pragma once

#include "Core/Types.hpp"

#include <psp2/gxm.h>

struct NVGcontext;
struct SceGxmContext;
struct SceGxmShaderPatcher;

void showPlayerOverlay();
void hidePlayerOverlay();
void tickPlayerOverlay();
void setMpvPause(bool paused);
void cycleMpvRotation();
void seekMpvRelative(double seconds);
void seekMpvAbsolute(double seconds);
void stopCurrentPlayback();

bool initMpv(SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher, SceGxmMultisampleMode msaa, NVGcontext* vg);
void shutdownMpv();
void renderMpvToFbo();
void drawVideo(NVGcontext* vg, float x, float y, float w, float h);
void playEntry(const SmbEntry& entry, int source, SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
               SceGxmMultisampleMode msaa, NVGcontext* vg);
void pollMpvEvents();
