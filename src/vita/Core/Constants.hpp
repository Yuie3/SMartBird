#pragma once

#include <cstdint>

constexpr float kWidth = 960.0f;
constexpr float kHeight = 544.0f;
constexpr float kPi = 3.14159265f;
constexpr int kNetMemSize = 1 * 1024 * 1024;
constexpr int kMaxEntries = 64;
constexpr int kVisibleEntries = 7;
constexpr float kListTopY = 92.0f;
constexpr float kListRowPitch = 58.0f;
constexpr float kListRowHeight = 54.0f;
constexpr int kMaxBrowserHistory = 16;
constexpr int kMaxHiddenItems = 256;
constexpr int kPlayerLogLines = 32;
constexpr uint64_t kMaxImageBytes = 32ull * 1024ull * 1024ull;
constexpr const char* kDataDir = "ux0:/data/Smbird";
constexpr const char* kDefaultLocalRoot = "ux0:";
constexpr const char* kLocalVideoCopyRoot = "ux0:/video";
constexpr const char* kLocalImageCopyRoot = "ux0:/picture";

#ifndef ALIGN
#define ALIGN(x, a) (((x) + ((a) - 1)) & ~((a) - 1))
#endif
