#pragma once

bool isHiddenOrSystemName(const char* name);
bool isVideoFile(const char* name);
bool isImageFile(const char* name);
bool isMediaFile(const char* name);
const char* fileExtensionLabel(const char* name);
