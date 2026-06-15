#pragma once

#include <cstddef>

const char* smbPathForDisplay();
void initCurrentPath();
void getConfiguredLocalRoot(char* out, size_t outSize);
void initLocalPath();
void getCurrentPath(char* out, size_t outSize);
bool currentPathIsRoot();
bool popBrowserFocus(int* selected, int* listTop);
void enterDirectory(const char* name, int selected, int listTop);
bool goParentDirectory();
void buildSmbFilePath(const char* fileName, char* out, size_t outSize);
void buildLocalFilePath(const char* fileName, char* out, size_t outSize);
