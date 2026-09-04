#pragma once

#include <array>

// Edit this file, then rebuild, to customize the overlay.

struct ScheduleTime {
    int hour;                 // 24-hour time: 13 means 1 PM.
    int minute;
    const wchar_t* label;     // Shown after "NEXT:".
};

struct RgbColor {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
};

// Add, remove, or change entries here. Keep them in chronological order.
constexpr std::array<ScheduleTime, 8> kSchedule{{
    {8, 50, L"08:50 AM"},
    {10, 5, L"10:05 AM"},
    {10, 15, L"10:15 AM"},
    {11, 30, L"11:30 AM"},
    {12, 30, L"12:30 PM"},
    {13, 45, L"01:45 PM"},
    {13, 55, L"01:55 PM"},
    {15, 10, L"03:10 PM"},
}};

// Text colour as RGB values from 0 to 255.
constexpr RgbColor kTextColor{245, 248, 255};

// Set to false to remove the text shadow completely.
constexpr bool kEnableTextShadow = true;
constexpr int kShadowOffsetX = 2;       // Positive X: right; negative X: left.
constexpr int kShadowOffsetY = 2;       // Positive Y: down; negative Y: up.
constexpr unsigned char kShadowOpacity = 150; // 0 = invisible, 255 = fully opaque.
