#pragma once

#include <cstddef>

char* connectFieldValue(int field);
size_t connectFieldSize(int field);
void initConnectionDefaults();
void loadConnectionConfig();
void saveConnectionConfig();
