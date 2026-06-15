#pragma once

void loadHiddenItems();
bool isUserHiddenItem(int source, const char* server, const char* share, const char* path, const char* name);
void addHiddenItem(int source, const char* server, const char* share, const char* path, const char* name);
void removeHiddenItemAt(int index);
