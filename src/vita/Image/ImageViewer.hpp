#pragma once

#include "Core/Types.hpp"

struct NVGcontext;

float fitImageScale(int width, int height, int rotationDegrees);
void clampImageViewLocked();
void resetImageView();
void closeImage(NVGcontext* vg);
bool openImageEntry(const SmbEntry& entry, int source, NVGcontext* vg, bool preserveHud = false);
void drawImageViewer(NVGcontext* vg, int font, const ImageState& image);
void zoomImage(float factor);
void panImage(float dx, float dy);
void rotateImageClockwise();
void toggleImageHud();
int findAdjacentImageIndex(const ScanState& scan, int selected, int direction);
