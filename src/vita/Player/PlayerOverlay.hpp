#pragma once

#include "Core/Types.hpp"

struct NVGcontext;

void drawPlayerOverlay(NVGcontext* vg, int font, const PlayerState& player, float viewW, float viewH);
