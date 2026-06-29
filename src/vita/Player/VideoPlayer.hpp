#pragma once

#include "Core/Types.hpp"

#include <psp2/gxm.h>

struct NVGcontext;
struct SceGxmContext;
struct SceGxmShaderPatcher;

void showPlayerOverlay();
void hidePlayerOverlay();
void togglePlayerOverlay();
void tickPlayerOverlay();
void setMpvPause(bool paused);
void setMpvSpeed(double speed);
void togglePlayerSpeedSlider();
void togglePlayerSettingsPanel();
void adjustMpvSpeed(int direction);
void cycleMpvSpeedPreset();
void toggleMpvLoop();
void toggleMpvShuffle();
void toggleMpvAutoRotate();
void updateMpvAutoRotation();
void seekMpvRelative(double seconds);
void seekMpvAbsolute(double seconds);
void stopCurrentPlayback();

bool initMpv(SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher, SceGxmMultisampleMode msaa, NVGcontext* vg);
void shutdownMpv();
void renderMpvToFbo(bool forceRedraw);
void drawVideo(NVGcontext* vg, float x, float y, float w, float h);
void playEntry(const SmbEntry& entry, int source, SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
               SceGxmMultisampleMode msaa, NVGcontext* vg);
void pollMpvEvents();
