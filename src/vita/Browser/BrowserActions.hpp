#pragma once

#include "Core/Types.hpp"

#include <psp2/gxm.h>

struct NVGcontext;
struct SceGxmContext;
struct SceGxmShaderPatcher;

void openBrowserEntryAtIndex(const ScanState& snapshot, int index, int* selected, int* listTop,
                             AppMode* mode, NVGcontext* vg,
                             SceGxmContext* gxmCtx, SceGxmShaderPatcher* patcher,
                             SceGxmMultisampleMode msaa);
void keepSelectedVisible(int selected, int count, int* listTop);
void keepSelectedNearListCenter(int selected, int count, int* listTop);
void scrollListByRows(int rows, int count, int* selected, int* listTop);
