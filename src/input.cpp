// input.cpp - keybind helpers and raw stream processing.
#include "input.h"

#include "raylib.h"

namespace input {

std::string BindName(int code) {
    if (code == 0) return "Unbound";
    if (IsMouseBind(code)) {
        static const char* const names[] = {"Mouse 1 (Left)", "Mouse 2 (Right)", "Mouse 3 (Middle)",
                                            "Mouse 4 (Back)", "Mouse 5 (Forward)"};
        return names[MouseBindButton(code)];
    }
    if (code >= KEY_A && code <= KEY_Z) return std::string(1, static_cast<char>('A' + (code - KEY_A)));
    if (code >= KEY_ZERO && code <= KEY_NINE) return std::string(1, static_cast<char>('0' + (code - KEY_ZERO)));
    if (code >= KEY_F1 && code <= KEY_F12) return "F" + std::to_string(code - KEY_F1 + 1);
    switch (code) {
        case KEY_SPACE: return "Space";
        case KEY_ESCAPE: return "Esc";
        case KEY_ENTER: return "Enter";
        case KEY_TAB: return "Tab";
        case KEY_BACKSPACE: return "Backspace";
        case KEY_INSERT: return "Insert";
        case KEY_DELETE: return "Delete";
        case KEY_HOME: return "Home";
        case KEY_END: return "End";
        case KEY_PAGE_UP: return "Page Up";
        case KEY_PAGE_DOWN: return "Page Down";
        case KEY_RIGHT: return "Right";
        case KEY_LEFT: return "Left";
        case KEY_DOWN: return "Down";
        case KEY_UP: return "Up";
        case KEY_LEFT_SHIFT: return "Left Shift";
        case KEY_RIGHT_SHIFT: return "Right Shift";
        case KEY_LEFT_CONTROL: return "Left Ctrl";
        case KEY_RIGHT_CONTROL: return "Right Ctrl";
        case KEY_LEFT_ALT: return "Left Alt";
        case KEY_RIGHT_ALT: return "Right Alt";
        case KEY_CAPS_LOCK: return "Caps Lock";
        case KEY_GRAVE: return "`";
        case KEY_MINUS: return "-";
        case KEY_EQUAL: return "=";
        case KEY_LEFT_BRACKET: return "[";
        case KEY_RIGHT_BRACKET: return "]";
        case KEY_BACKSLASH: return "\\";
        case KEY_SEMICOLON: return ";";
        case KEY_APOSTROPHE: return "'";
        case KEY_COMMA: return ",";
        case KEY_PERIOD: return ".";
        case KEY_SLASH: return "/";
        default: break;
    }
    if (code >= KEY_KP_0 && code <= KEY_KP_9) return "Num " + std::to_string(code - KEY_KP_0);
    return "Key " + std::to_string(code);
}

bool BindPressed(int code) {
    if (code <= 0) return false;
    if (IsMouseBind(code)) return IsMouseButtonPressed(MouseBindButton(code));
    return IsKeyPressed(code);
}

bool BindDown(int code) {
    if (code <= 0) return false;
    if (IsMouseBind(code)) return IsMouseButtonDown(MouseBindButton(code));
    return IsKeyDown(code);
}

int CaptureBind() {
    if (IsKeyPressed(KEY_ESCAPE)) return -1;
    for (int b = 0; b < 5; ++b) {
        if (IsMouseButtonPressed(b)) return MouseBind(b);
    }
    // raylib key codes are sparse; scanning 1..399 covers all of them.
    for (int k = 1; k < 400; ++k) {
        if (k == KEY_ESCAPE) continue;
        if (IsKeyPressed(k)) return k;
    }
    return 0;
}

RawStreamResult ProcessRawStream(const std::vector<platform::RawEvent>& events, ValCamera& cam,
                                 AimHistory& history, double sens, int shootBind, bool& triggerHeld,
                                 const std::function<double(double)>& toGameTime,
                                 const std::function<void(double)>& onShot) {
    RawStreamResult r;
    const bool mouseShoot = IsMouseBind(shootBind);
    const int shootButton = mouseShoot ? MouseBindButton(shootBind) : -1;

    for (const platform::RawEvent& e : events) {
        const double t = toGameTime(e.time);
        if (e.button < 0) {
            cam.ApplyCounts(e.dx, e.dy, sens);
            history.Push(t, cam.Yaw(), cam.Pitch());
            r.totalDx += e.dx;
            r.totalDy += e.dy;
        } else if (e.button == shootButton) {
            if (e.down && !triggerHeld) {
                triggerHeld = true;
                if (onShot) onShot(t);
            } else if (!e.down) {
                triggerHeld = false;
            }
        }
    }
    return r;
}

}  // namespace input
