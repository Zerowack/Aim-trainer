// platform.h - Thin wrapper around the Win32 pieces the trainer needs.
//
// windows.h and raylib.h cannot be included in the same translation unit
// (both define names such as Rectangle, CloseWindow, DrawText, ShowCursor).
// Everything Win32-specific therefore lives in platform_win32.cpp, and the
// rest of the program only talks to this small, raylib-free interface.
#pragma once

#include <vector>

namespace platform {

// Mouse buttons as reported by Raw Input.
enum RawButton : int {
    kRawLeft = 0,
    kRawRight = 1,
    kRawMiddle = 2,
    kRawX1 = 3,
    kRawX2 = 4,
    kRawButtonCount = 5
};

// One entry in the ordered raw mouse stream.
// Either a movement chunk (button == -1, dx/dy = raw counts) or a button
// transition (button >= 0, dx = dy = 0). Consecutive movement packets are
// merged, but a button event always splits the stream, so the game can apply
// exactly the movement that happened *before* each click.
struct RawEvent {
    long dx = 0;
    long dy = 0;
    int button = -1;
    bool down = false;
    double time = 0.0;  // platform::Now() when the packet was processed
};

// Must be called before InitWindow(): opts into per-monitor DPI awareness so
// Windows never rescales our window or mouse coordinates.
void EnableDpiAwareness();

// Hooks the raylib/GLFW window (GetWindowHandle()) and registers for raw
// mouse input. Returns false if raw input could not be registered.
bool Init(void* nativeWindowHandle);
void Shutdown();

// Seconds since program start, from QueryPerformanceCounter.
double Now();

// Dispatches all pending window messages (raw input, keyboard, focus ...).
// Called at the very start of each frame so input is as fresh as possible.
void PumpMessages();

// Moves all raw events collected since the last call into 'out'.
void TakeRawEvents(std::vector<RawEvent>& out);

// Confines the (hidden) cursor to the center of the window while locked.
void SetCursorLocked(bool locked);
bool IsCursorLocked();
// Re-applies the confinement rectangle (window moved, resolution changed,
// another program reset the clip). Cheap; call once per frame.
void UpdateCursorLock();

// Focus tracking. ConsumeFocusLost() returns true once after each focus loss.
bool HasFocus();
bool ConsumeFocusLost();
bool QuitRequested();

// Forces the OpenGL swap interval (0 = vsync off) via wglSwapIntervalEXT.
void SetVSync(bool enabled);

// Sleeps/spins until Now() >= targetTime with sub-millisecond precision.
void PreciseWaitUntil(double targetTime);

void ShowErrorBox(const char* title, const char* message);

}  // namespace platform
