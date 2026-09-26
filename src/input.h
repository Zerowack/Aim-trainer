// input.h - Keybinds and processing of the raw mouse stream.
//
// A "bind code" is a single int:
//   1 .. 999        a raylib keyboard key (KEY_R, KEY_F2, ...)
//   1000 + button   a mouse button (0 = left, 1 = right, 2 = middle, 3 = X1/back, 4 = X2/forward)
//   0               unbound
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "camera.h"
#include "platform.h"

namespace input {

constexpr int kMouseBindBase = 1000;

inline int MouseBind(int button) { return kMouseBindBase + button; }
inline bool IsMouseBind(int code) { return code >= kMouseBindBase && code < kMouseBindBase + 5; }
inline int MouseBindButton(int code) { return code - kMouseBindBase; }

// Human readable name ("Mouse 1", "R", "F2", ...).
std::string BindName(int code);

// raylib based state (used by menus and keyboard binds).
bool BindPressed(int code);
bool BindDown(int code);

// For the rebinding UI: returns the code of any key/mouse button pressed this
// frame, -1 if Esc was pressed (cancel), or 0 if nothing was pressed.
int CaptureBind();

// Walks the ordered raw stream collected this frame. Every movement chunk
// rotates the camera immediately (and is recorded in the aim history); every
// press of the shoot binding calls onShot with the camera already rotated by
// all movement that happened before the click. Other mouse buttons go to
// onButton (e.g. scoping), so a scope-in mid-frame changes the sensitivity
// for exactly the packets after it. 'sens' is read for every movement chunk.
// 'toGameTime' converts platform::Now() timestamps to the game clock.
struct RawStreamResult {
    long totalDx = 0;
    long totalDy = 0;
};

RawStreamResult ProcessRawStream(const std::vector<platform::RawEvent>& events, ValCamera& cam,
                                 AimHistory& history, const std::function<double()>& sens, int shootBind,
                                 bool& triggerHeld, const std::function<double(double)>& toGameTime,
                                 const std::function<void(double)>& onShot,
                                 const std::function<void(int, bool, double)>& onButton);

}  // namespace input
