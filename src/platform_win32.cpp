// platform_win32.cpp - Raw Input, high resolution timing, cursor confinement
// and focus handling implemented directly with the Win32 API.
//
// How mouse input works here:
//   * RegisterRawInputDevices() asks Windows to send WM_INPUT messages with the
//     untouched counts reported by the mouse. Pointer speed, "Enhance pointer
//     precision" and any other acceleration only affect the *cursor*, never raw
//     input - this is the same data Valorant reads.
//   * raylib (through GLFW) owns the window procedure, so we subclass it: our
//     WndProc sees every message first, handles WM_INPUT and focus changes, and
//     then forwards the message to GLFW's original procedure.
//   * Every WM_INPUT packet is appended to a queue. Nothing is ever averaged or
//     dropped, so at 8000 Hz polling all ~8000 packets per second are summed.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform.h"

#include <cwctype>
#include <string>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace platform {
namespace {

HWND g_hwnd = nullptr;
WNDPROC g_originalProc = nullptr;

LARGE_INTEGER g_qpcFrequency = {};
LARGE_INTEGER g_qpcStart = {};
bool g_timerReady = false;

std::vector<RawEvent> g_events;

bool g_cursorLocked = false;
bool g_focused = true;
bool g_focusLostPending = false;
bool g_quitRequested = false;
RECT g_lastClip = {0, 0, 0, 0};
double g_lastClipTime = -1.0;

// Absolute-mode devices (remote desktop, some tablets / VMs) report positions
// instead of deltas; we convert those into deltas so the app still works.
bool g_haveAbsolute = false;
LONG g_lastAbsX = 0;
LONG g_lastAbsY = 0;

HANDLE g_waitTimer = nullptr;

// Safety limit: if the queue is not drained for a long time (should never
// happen), movement is merged instead of growing without bound.
constexpr size_t kMaxQueuedEvents = 1u << 16;

void EnsureTimer() {
    if (!g_timerReady) {
        QueryPerformanceFrequency(&g_qpcFrequency);
        QueryPerformanceCounter(&g_qpcStart);
        g_timerReady = true;
    }
}

void PushMove(long dx, long dy, double t) {
    if (dx == 0 && dy == 0) return;
    if (!g_events.empty() && g_events.back().button < 0) {
        // Merge with the previous movement chunk (no click in between).
        g_events.back().dx += dx;
        g_events.back().dy += dy;
        g_events.back().time = t;
        return;
    }
    if (g_events.size() >= kMaxQueuedEvents) {
        // Extremely unlikely: fold into the last event, never lose counts.
        g_events.back().dx += dx;
        g_events.back().dy += dy;
        return;
    }
    RawEvent e;
    e.dx = dx;
    e.dy = dy;
    e.button = -1;
    e.time = t;
    g_events.push_back(e);
}

void PushButton(int button, bool down, double t) {
    if (g_events.size() >= kMaxQueuedEvents) return;
    RawEvent e;
    e.button = button;
    e.down = down;
    e.time = t;
    g_events.push_back(e);
}

void HandleRawInput(LPARAM lParam) {
    RAWINPUT raw;
    UINT size = sizeof(raw);
    const UINT got = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size,
                                     sizeof(RAWINPUTHEADER));
    if (got == static_cast<UINT>(-1) || got == 0) return;
    if (raw.header.dwType != RIM_TYPEMOUSE) return;

    const double t = Now();
    const RAWMOUSE& m = raw.data.mouse;

    if (m.usFlags & MOUSE_MOVE_ABSOLUTE) {
        const bool virtualDesk = (m.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const int w = GetSystemMetrics(virtualDesk ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const int h = GetSystemMetrics(virtualDesk ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        const LONG ax = MulDiv(m.lLastX, w, 65535);
        const LONG ay = MulDiv(m.lLastY, h, 65535);
        if (g_haveAbsolute) PushMove(ax - g_lastAbsX, ay - g_lastAbsY, t);
        g_lastAbsX = ax;
        g_lastAbsY = ay;
        g_haveAbsolute = true;
    } else {
        PushMove(m.lLastX, m.lLastY, t);
    }

    const USHORT f = m.usButtonFlags;
    if (f & RI_MOUSE_LEFT_BUTTON_DOWN) PushButton(kRawLeft, true, t);
    if (f & RI_MOUSE_LEFT_BUTTON_UP) PushButton(kRawLeft, false, t);
    if (f & RI_MOUSE_RIGHT_BUTTON_DOWN) PushButton(kRawRight, true, t);
    if (f & RI_MOUSE_RIGHT_BUTTON_UP) PushButton(kRawRight, false, t);
    if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN) PushButton(kRawMiddle, true, t);
    if (f & RI_MOUSE_MIDDLE_BUTTON_UP) PushButton(kRawMiddle, false, t);
    if (f & RI_MOUSE_BUTTON_4_DOWN) PushButton(kRawX1, true, t);
    if (f & RI_MOUSE_BUTTON_4_UP) PushButton(kRawX1, false, t);
    if (f & RI_MOUSE_BUTTON_5_DOWN) PushButton(kRawX2, true, t);
    if (f & RI_MOUSE_BUTTON_5_UP) PushButton(kRawX2, false, t);
}

void OnFocusLost() {
    g_focused = false;
    g_focusLostPending = true;
    // Release the cursor immediately so alt-tab never leaves it trapped.
    ClipCursor(nullptr);
    g_lastClipTime = -1.0;
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INPUT:
            // Raw input only arrives while this window is in the foreground,
            // so it also proves we have focus again (some overlays / screenshot
            // tools give focus back without the usual activation messages).
            g_focused = true;
            HandleRawInput(lParam);
            break;  // Forward so DefWindowProc can clean up the input buffer.
        case WM_ACTIVATEAPP:
            if (wParam == FALSE) OnFocusLost();
            else g_focused = true;
            break;
        case WM_KILLFOCUS:
            OnFocusLost();
            break;
        case WM_SETFOCUS:
            g_focused = true;
            break;
        default:
            break;
    }
    return CallWindowProcW(g_originalProc, hwnd, msg, wParam, lParam);
}

bool RegisterMouse(HWND target) {
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;  // HID_USAGE_PAGE_GENERIC
    rid.usUsage = 0x02;      // HID_USAGE_GENERIC_MOUSE
    rid.dwFlags = 0;         // Only while our window is in the foreground.
    rid.hwndTarget = target;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

// Calls a function pointer obtained from GetProcAddress / wglGetProcAddress.
// Casting through void(*)() is the portable, warning-free way to do this.
template <typename Fn, typename Proc>
Fn CastProc(Proc p) {
    return reinterpret_cast<Fn>(reinterpret_cast<void (*)()>(p));
}

}  // namespace

void EnableDpiAwareness() {
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) return;
    using SetCtxFn = BOOL(WINAPI*)(HANDLE);
    auto setCtx = CastProc<SetCtxFn>(GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setCtx) {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        if (setCtx(reinterpret_cast<HANDLE>(static_cast<LONG_PTR>(-4)))) return;
    }
    using SetAwareFn = BOOL(WINAPI*)();
    auto setAware = CastProc<SetAwareFn>(GetProcAddress(user32, "SetProcessDPIAware"));
    if (setAware) setAware();
}

bool Init(void* nativeWindowHandle) {
    EnsureTimer();
    g_hwnd = static_cast<HWND>(nativeWindowHandle);
    if (!g_hwnd) return false;

    g_originalProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassProc)));
    g_focused = (GetForegroundWindow() == g_hwnd);
    g_events.reserve(4096);

    g_waitTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);
    return RegisterMouse(g_hwnd);
}

void Shutdown() {
    ClipCursor(nullptr);
    if (g_hwnd && g_originalProc) {
        SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_originalProc));
    }
    RAWINPUTDEVICE rid;
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    if (g_waitTimer) CloseHandle(g_waitTimer);
    g_waitTimer = nullptr;
    g_originalProc = nullptr;
    g_hwnd = nullptr;
}

double Now() {
    EnsureTimer();
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return static_cast<double>(t.QuadPart - g_qpcStart.QuadPart) /
           static_cast<double>(g_qpcFrequency.QuadPart);
}

void PumpMessages() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_quitRequested = true;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

void TakeRawEvents(std::vector<RawEvent>& out) {
    out.clear();
    out.swap(g_events);
}

void SetCursorLocked(bool locked) {
    if (locked == g_cursorLocked) return;
    g_cursorLocked = locked;
    g_lastClipTime = -1.0;
    if (!locked) {
        ClipCursor(nullptr);
    } else {
        UpdateCursorLock();
    }
    g_haveAbsolute = false;
}

bool IsCursorLocked() { return g_cursorLocked; }

void UpdateCursorLock() {
    if (!g_hwnd) return;
    if (!g_cursorLocked || !HasFocus()) return;

    RECT client;
    GetClientRect(g_hwnd, &client);
    POINT center = {(client.right - client.left) / 2, (client.bottom - client.top) / 2};
    ClientToScreen(g_hwnd, &center);
    RECT clip = {center.x, center.y, center.x + 1, center.y + 1};

    // Re-apply when the rectangle changed, or periodically in case another
    // program reset the clip region.
    const double now = Now();
    const bool changed = clip.left != g_lastClip.left || clip.top != g_lastClip.top;
    if (changed || g_lastClipTime < 0.0 || now - g_lastClipTime > 0.25) {
        SetCursorPos(center.x, center.y);
        ClipCursor(&clip);
        g_lastClip = clip;
        g_lastClipTime = now;
    }
}

bool HasFocus() {
    // Ask Windows directly instead of trusting only the focus messages.
    if (!g_hwnd) return g_focused;
    if (IsIconic(g_hwnd)) return false;
    const bool fg = GetForegroundWindow() == g_hwnd;
    if (fg) g_focused = true;
    return fg;
}

Foreground ForegroundKind() {
    HWND fg = GetForegroundWindow();
    if (!fg) return Foreground::None;
    if (fg == g_hwnd) return Foreground::Us;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!proc) return Foreground::Other;
    wchar_t path[MAX_PATH] = {};
    DWORD len = MAX_PATH;
    const BOOL ok = QueryFullProcessImageNameW(proc, 0, path, &len);
    CloseHandle(proc);
    if (!ok) return Foreground::Other;
    const wchar_t* name = path;
    for (const wchar_t* p = path; *p; ++p) {
        if (*p == L'\\' || *p == L'/') name = p + 1;
    }
    std::wstring lower(name);
    for (wchar_t& c : lower) c = static_cast<wchar_t>(towlower(c));
    static const wchar_t* const tools[] = {L"snippingtool.exe",  L"screenclippinghost.exe", L"screensketch.exe",
                                           L"sharex.exe",        L"lightshot.exe",          L"greenshot.exe",
                                           L"gyazo.exe",         L"flameshot.exe",          L"snagit32.exe",
                                           L"snagiteditor.exe",  L"gamebar.exe",            L"gamebarftserver.exe",
                                           L"nvidia share.exe",  L"nvidia overlay.exe",     L"picpick.exe"};
    for (const wchar_t* t : tools) {
        if (lower == t) return Foreground::CaptureTool;
    }
    return Foreground::Other;
}

bool CopyImageToClipboard(const unsigned char* rgba, int width, int height) {
    if (!rgba || width <= 0 || height <= 0 || !g_hwnd) return false;
    const size_t pixels = static_cast<size_t>(width) * static_cast<size_t>(height);
    const size_t bytes = sizeof(BITMAPINFOHEADER) + pixels * 4;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) return false;
    auto* hdr = static_cast<BITMAPINFOHEADER*>(GlobalLock(mem));
    if (!hdr) {
        GlobalFree(mem);
        return false;
    }
    ZeroMemory(hdr, sizeof(BITMAPINFOHEADER));
    hdr->biSize = sizeof(BITMAPINFOHEADER);
    hdr->biWidth = width;
    hdr->biHeight = height;  // bottom-up
    hdr->biPlanes = 1;
    hdr->biBitCount = 32;
    hdr->biCompression = BI_RGB;
    unsigned char* dst = reinterpret_cast<unsigned char*>(hdr + 1);
    for (int y = 0; y < height; ++y) {
        const unsigned char* src = rgba + static_cast<size_t>(height - 1 - y) * static_cast<size_t>(width) * 4;
        unsigned char* row = dst + static_cast<size_t>(y) * static_cast<size_t>(width) * 4;
        for (int x = 0; x < width; ++x) {
            row[x * 4 + 0] = src[x * 4 + 2];
            row[x * 4 + 1] = src[x * 4 + 1];
            row[x * 4 + 2] = src[x * 4 + 0];
            row[x * 4 + 3] = 255;
        }
    }
    GlobalUnlock(mem);
    if (!OpenClipboard(g_hwnd)) {
        GlobalFree(mem);
        return false;
    }
    EmptyClipboard();
    const bool ok = SetClipboardData(CF_DIB, mem) != nullptr;
    CloseClipboard();
    if (!ok) GlobalFree(mem);  // on success the clipboard owns the memory
    return ok;
}

namespace {
struct BackgroundJob {
    void (*fn)(void*);
    void* arg;
};
DWORD WINAPI BackgroundThread(LPVOID p) {
    BackgroundJob* job = static_cast<BackgroundJob*>(p);
    job->fn(job->arg);
    delete job;
    return 0;
}
}  // namespace

void RunInBackground(void (*fn)(void*), void* arg) {
    BackgroundJob* job = new BackgroundJob{fn, arg};
    HANDLE h = CreateThread(nullptr, 0, &BackgroundThread, job, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        fn(arg);  // no thread: do it now
        delete job;
    }
}

void AllowMinimize() {
    if (!g_hwnd) return;
    // Borderless / fullscreen windows are created without a minimize box, so
    // clicking the taskbar button or Win+Down did nothing. Add it (it isn't
    // drawn on a window without a title bar).
    const LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_STYLE);
    const LONG_PTR want = style | WS_MINIMIZEBOX | WS_SYSMENU;
    if (want != style) {
        SetWindowLongPtrW(g_hwnd, GWL_STYLE, want);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
}

bool ConsumeFocusLost() {
    const bool v = g_focusLostPending;
    g_focusLostPending = false;
    return v;
}

bool QuitRequested() { return g_quitRequested; }

void SetVSync(bool enabled) {
    using SwapIntervalFn = BOOL(WINAPI*)(int);
    auto fn = CastProc<SwapIntervalFn>(wglGetProcAddress("wglSwapIntervalEXT"));
    if (fn) fn(enabled ? 1 : 0);
}

void PreciseWaitUntil(double targetTime) {
    for (;;) {
        const double remaining = targetTime - Now();
        if (remaining <= 0.0) return;
        if (remaining > 0.0015) {
            // Coarse part: let the OS sleep, waking ~1 ms early.
            const double sleepFor = remaining - 0.001;
            if (g_waitTimer) {
                LARGE_INTEGER due;
                due.QuadPart = -static_cast<LONGLONG>(sleepFor * 1.0e7);  // 100 ns units, relative
                if (SetWaitableTimer(g_waitTimer, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(g_waitTimer, INFINITE);
                    continue;
                }
            }
            Sleep(1);
        } else {
            // Fine part: spin for exact timing.
            YieldProcessor();
        }
    }
}

void ShowErrorBox(const char* title, const char* message) {
    MessageBoxA(g_hwnd, message, title, MB_OK | MB_ICONERROR);
}

}  // namespace platform
