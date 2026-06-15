#pragma once

#include <cstddef>

void copyText(char* dst, size_t dstSize, const char* src);
void trimLine(char* line);
const char* optionalText(const char* value);
char asciiLower(char c);
bool endsWithNoCase(const char* text, const char* suffix);
bool containsNoCase(const char* text, const char* needle);
bool equalsNoCase(const char* a, const char* b);
