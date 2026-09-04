// NextBellCountdown.cpp
// A transparent, click-through countdown overlay for Windows 10/11.

#include <windows.h>

#include "NextBellCountdownConfig.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace {

constexpr UINT_PTR kRefreshTimer = 1;
constexpr UINT kRefreshIntervalMs = 10;
constexpr int kWindowWidth = 330;
constexpr int kWindowHeight = 78;
constexpr int kScreenMargin = 18;

struct NextEvent {
    FILETIME at;
    const wchar_t* label;
};

HFONT g_titleFont = nullptr;
HFONT g_timeFont = nullptr;

unsigned long long FileTimeToUint64(const FILETIME& fileTime) {
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return value.QuadPart;
}

FILETIME PreciseNow() {
    // Available on Windows 8 and later. It avoids the coarse resolution of GetSystemTimeAsFileTime.
    FILETIME now{};
    GetSystemTimePreciseAsFileTime(&now);
    return now;
}

bool LocalSystemTimeToFileTime(const SYSTEMTIME& localTime, FILETIME* result) {
    SYSTEMTIME utcTime{};
    if (!TzSpecificLocalTimeToSystemTime(nullptr, &localTime, &utcTime)) {
        return false;
    }
    return SystemTimeToFileTime(&utcTime, result) != FALSE;
}

NextEvent FindNextEvent() {
    const FILETIME now = PreciseNow();
    const unsigned long long nowValue = FileTimeToUint64(now);

    SYSTEMTIME localNow{};
    GetLocalTime(&localNow);

    for (const auto& scheduled : kSchedule) {
        SYSTEMTIME candidate = localNow;
        candidate.wHour = static_cast<WORD>(scheduled.hour);
        candidate.wMinute = static_cast<WORD>(scheduled.minute);
        candidate.wSecond = 0;
        candidate.wMilliseconds = 0;

        FILETIME candidateFileTime{};
        if (LocalSystemTimeToFileTime(candidate, &candidateFileTime) &&
            FileTimeToUint64(candidateFileTime) > nowValue) {
            return {candidateFileTime, scheduled.label};
        }
    }

    // Today is over: advance the local calendar date, then convert the desired
    // local time to UTC. The conversion happens after the date calculation, so
    // a daylight-saving transition cannot make "tomorrow 08:50" one hour off.
    SYSTEMTIME firstTomorrow = localNow;
    FILETIME calendarFileTime{};
    SystemTimeToFileTime(&firstTomorrow, &calendarFileTime);
    ULARGE_INTEGER calendarValue{};
    calendarValue.LowPart = calendarFileTime.dwLowDateTime;
    calendarValue.HighPart = calendarFileTime.dwHighDateTime;
    calendarValue.QuadPart += 24ULL * 60 * 60 * 10'000'000;
    calendarFileTime.dwLowDateTime = calendarValue.LowPart;
    calendarFileTime.dwHighDateTime = calendarValue.HighPart;
    FileTimeToSystemTime(&calendarFileTime, &firstTomorrow);

    firstTomorrow.wHour = static_cast<WORD>(kSchedule.front().hour);
    firstTomorrow.wMinute = static_cast<WORD>(kSchedule.front().minute);
    firstTomorrow.wSecond = 0;
    firstTomorrow.wMilliseconds = 0;

    FILETIME eventTime{};
    LocalSystemTimeToFileTime(firstTomorrow, &eventTime);
    return {eventTime, kSchedule.front().label};
}

std::wstring CountdownText(const NextEvent& event, const FILETIME& nowFileTime) {
    const unsigned long long now = FileTimeToUint64(nowFileTime);
    const unsigned long long target = FileTimeToUint64(event.at);
    const unsigned long long remainingMs = target > now ? (target - now) / 10'000ULL : 0;

    const unsigned long long hours = remainingMs / 3'600'000ULL;
    const unsigned long long minutes = (remainingMs / 60'000ULL) % 60ULL;
    const unsigned long long seconds = (remainingMs / 1'000ULL) % 60ULL;
    const unsigned long long milliseconds = remainingMs % 1'000ULL;

    wchar_t text[64]{};
    swprintf_s(text, L"%02llu:%02llu:%02llu.%03llu", hours, minutes, seconds, milliseconds);
    return text;
}

void MoveToBottomRight(HWND window) {
    MONITORINFO monitorInfo{sizeof(monitorInfo)};
    const HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(monitor, &monitorInfo)) return;

    const RECT& work = monitorInfo.rcWork;
    SetWindowPos(window, HWND_TOPMOST,
                 work.right - kWindowWidth - kScreenMargin,
                 work.bottom - kWindowHeight - kScreenMargin,
                 kWindowWidth, kWindowHeight,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void DrawTextLine(HDC dc, const RECT& bounds, const std::wstring& text, HFONT font, UINT format) {
    const HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255)); // White text is used as an alpha mask.
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, text.c_str(), -1, const_cast<RECT*>(&bounds), format);
    SelectObject(dc, oldFont);
}

// Render into 32-bit bitmaps and turn gray text coverage into pixel alpha.
// A color-keyed layered window cannot represent ClearType's colored sub-pixels;
// UpdateLayeredWindow with ULW_ALPHA gives the text genuine transparent edges.
void RenderOverlay(HWND window) {
    HDC screenDc = GetDC(nullptr);
    HDC memoryDc = CreateCompatibleDC(screenDc);
    HDC shadowDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bitmapInfo{};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = kWindowWidth;
    bitmapInfo.bmiHeader.biHeight = -kWindowHeight; // top-down DIB
    bitmapInfo.bmiHeader.biPlanes = 1;
    bitmapInfo.bmiHeader.biBitCount = 32;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    void* rawPixels = nullptr;
    void* rawShadowPixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS, &rawPixels, nullptr, 0);
    HBITMAP shadowBitmap = CreateDIBSection(screenDc, &bitmapInfo, DIB_RGB_COLORS,
                                             &rawShadowPixels, nullptr, 0);
    if (!bitmap || !shadowBitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (shadowBitmap) DeleteObject(shadowBitmap);
        DeleteDC(memoryDc);
        DeleteDC(shadowDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }

    const HGDIOBJ previousBitmap = SelectObject(memoryDc, bitmap);
    const HGDIOBJ previousShadowBitmap = SelectObject(shadowDc, shadowBitmap);
    RECT client{0, 0, kWindowWidth, kWindowHeight};
    FillRect(memoryDc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    FillRect(shadowDc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

    const NextEvent event = FindNextEvent();
    const FILETIME now = PreciseNow();
    RECT title{0, 3, kWindowWidth, 28};
    DrawTextLine(memoryDc, title, std::wstring(L"NEXT: ") + event.label, g_titleFont,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    RECT countdown{0, 28, kWindowWidth, kWindowHeight - 2};
    DrawTextLine(memoryDc, countdown, CountdownText(event, now), g_timeFont,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    if (kEnableTextShadow) {
        RECT shadowTitle = title;
        RECT shadowCountdown = countdown;
        OffsetRect(&shadowTitle, kShadowOffsetX, kShadowOffsetY);
        OffsetRect(&shadowCountdown, kShadowOffsetX, kShadowOffsetY);
        DrawTextLine(shadowDc, shadowTitle, std::wstring(L"NEXT: ") + event.label, g_titleFont,
                     DT_CENTER | DT_SINGLELINE | DT_VCENTER);
        DrawTextLine(shadowDc, shadowCountdown, CountdownText(event, now), g_timeFont,
                     DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    }

    // The source bitmaps are grayscale text masks. Draw the main text in a soft
    // white and composite a black shadow underneath it using premultiplied ARGB.
    auto* pixels = static_cast<DWORD*>(rawPixels);
    const auto* shadowPixels = static_cast<const DWORD*>(rawShadowPixels);
    for (int i = 0; i < kWindowWidth * kWindowHeight; ++i) {
        const DWORD mainPixel = pixels[i];
        const DWORD shadowPixel = shadowPixels[i];
        const BYTE mainAlpha = std::max({
            static_cast<BYTE>(mainPixel & 0xFF),
            static_cast<BYTE>((mainPixel >> 8) & 0xFF),
            static_cast<BYTE>((mainPixel >> 16) & 0xFF)});
        const BYTE shadowCoverage = std::max({
            static_cast<BYTE>(shadowPixel & 0xFF),
            static_cast<BYTE>((shadowPixel >> 8) & 0xFF),
            static_cast<BYTE>((shadowPixel >> 16) & 0xFF)});
        const unsigned shadowAlpha = kEnableTextShadow
            ? (static_cast<unsigned>(shadowCoverage) * kShadowOpacity) / 255U
            : 0U;
        const unsigned outputAlpha = static_cast<unsigned>(mainAlpha) +
            (shadowAlpha * (255U - static_cast<unsigned>(mainAlpha))) / 255U;
        const unsigned red = (static_cast<unsigned>(kTextColor.red) * mainAlpha) / 255U;
        const unsigned green = (static_cast<unsigned>(kTextColor.green) * mainAlpha) / 255U;
        const unsigned blue = (static_cast<unsigned>(kTextColor.blue) * mainAlpha) / 255U;
        pixels[i] = (outputAlpha << 24) | (red << 16) | (green << 8) | blue;
    }

    SIZE size{kWindowWidth, kWindowHeight};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(window, screenDc, nullptr, &size, memoryDc, &source, 0, &blend, ULW_ALPHA);

    SelectObject(memoryDc, previousBitmap);
    SelectObject(shadowDc, previousShadowBitmap);
    DeleteObject(bitmap);
    DeleteObject(shadowBitmap);
    DeleteDC(memoryDc);
    DeleteDC(shadowDc);
    ReleaseDC(nullptr, screenDc);
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        g_titleFont = CreateFontW(17, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        g_timeFont = CreateFontW(31, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        SetTimer(window, kRefreshTimer, kRefreshIntervalMs, nullptr);
        MoveToBottomRight(window);
        RenderOverlay(window);
        return 0;

    case WM_TIMER:
        RenderOverlay(window);
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        MoveToBottomRight(window);
        RenderOverlay(window);
        return 0;

    case WM_HOTKEY:
        if (wParam == 1) DestroyWindow(window); // Ctrl + Alt + Q
        return 0;

    case WM_DESTROY:
        KillTimer(window, kRefreshTimer);
        UnregisterHotKey(window, 1);
        DeleteObject(g_titleFont);
        DeleteObject(g_timeFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    constexpr wchar_t kClassName[] = L"NextBellCountdownOverlay";

    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpszClassName = kClassName;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassW(&windowClass);

    HWND window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT,
        kClassName, L"Next bell countdown", WS_POPUP,
        0, 0, kWindowWidth, kWindowHeight, nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    RegisterHotKey(window, 1, MOD_CONTROL | MOD_ALT, 'Q');
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
