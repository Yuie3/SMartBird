#pragma once

#include "Core/Types.hpp"

struct NVGcontext;

void renderUi(NVGcontext* vg, int font, const RuntimeStatus& runtime, const ScanState& scan,
              const PlayerState& player, const ImageState& image,
              AppMode mode, int selected, int listTop, int connectFocus, int actionPulseFrames);
