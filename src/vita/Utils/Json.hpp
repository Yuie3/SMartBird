#pragma once

#include <cstddef>
#include <cstdio>

void writeJsonString(FILE* fp, const char* text);
const char* skipJsonSpace(const char* p);
bool parseJsonString(const char** cursor, char* out, size_t outSize);
bool consumeJsonChar(const char** cursor, char expected);
void skipJsonValue(const char** cursor);
