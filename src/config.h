// config.h - All user settings, stored in config.ini next to the .exe.
#pragma once

#include <string>

#include "crosshair.h"
#include "raylib.h"

enum class DisplayMode : int { Fullscreen = 0, Borderless = 1, Windowed = 2 };

// FPS cap choices. Index 0 = uncapped, the last one = custom value.
constexpr int kFpsCapCount = 5;
constexpr int kFpsCapCustomIndex = kFpsCapCount - 1;
constexpr int kCustomFpsMin = 30;
constexpr int kCustomFpsMax = 2000;
extern const int kFpsCapValues[kFpsCapCount];    // 0, 144, 240, 360, (custom)
extern const char* const kFpsCapNames[kFpsCapCount];

struct Keybinds {
    int shoot;     // see input.h for how mouse buttons are encoded
    int restart;
    int pause;     // Esc always pauses too; this is an extra key
    int toggleFps;
    int scope;     // Sniper mode (default Mouse 2)
};

struct Config {
    bool firstRunDone = false;

    // Sensitivity
    double dpi = 800.0;
    double sens = 0.4;
    // Valorant's "Scoped Sensitivity Multiplier" (0.01 .. 10, default 1).
    double scopedMult = 1.0;
    // Apply the coach's over/undershoot sens suggestion automatically after runs.
    bool autoSens = false;

    // Video
    DisplayMode displayMode = DisplayMode::Fullscreen;
    int windowWidth = 1600;
    int windowHeight = 900;
    int fpsCapIndex = 0;      // uncapped by default
    int customFpsCap = 165;   // used when fpsCapIndex == kFpsCapCustomIndex
    bool showFps = true;
    bool showFovInfo = true;

    // Gameplay
    int runSeconds = 60;
    Color targetColor = Color{80, 220, 255, 255};
    float mapBrightness = 1.0f;  // 0.2 .. 1.6
    bool roastMode = true;
    int difficulty = 1;          // last picked difficulty (0 easy .. 3 insane)
    int sniperWeapon = 2;        // 0 Marshal, 1 Outlaw, 2 Operator
    bool scopeHold = false;      // hold to scope (Valorant "Hold to aim down sights")       // funny rank comments ("your aim is ...")

    // Audio
    float masterVolume = 0.8f;
    float hitVolume = 0.7f;

    Keybinds keys = {};

    Crosshair crosshair;

    Config();
    // Frame cap in FPS for gameplay (0 = uncapped).
    int FpsCap() const;
    // Missing files/keys simply keep defaults. Returns false if no file existed.
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};
