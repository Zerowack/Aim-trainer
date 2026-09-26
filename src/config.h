// config.h - All user settings, stored in config.ini next to the .exe.
#pragma once

#include <string>

#include "crosshair.h"
#include "raylib.h"

enum class DisplayMode : int { Fullscreen = 0, Borderless = 1, Windowed = 2 };

// FPS cap choices. Index 0 = uncapped.
constexpr int kFpsCapCount = 4;
extern const int kFpsCapValues[kFpsCapCount];    // 0, 144, 240, 360
extern const char* const kFpsCapNames[kFpsCapCount];

struct Keybinds {
    int shoot;     // see input.h for how mouse buttons are encoded
    int restart;
    int pause;     // Esc always pauses too; this is an extra key
    int toggleFps;
};

struct Config {
    bool firstRunDone = false;

    // Sensitivity
    double dpi = 800.0;
    double sens = 0.4;

    // Video
    DisplayMode displayMode = DisplayMode::Fullscreen;
    int windowWidth = 1600;
    int windowHeight = 900;
    int fpsCapIndex = 0;      // uncapped by default
    bool showFps = true;
    bool showFovInfo = true;

    // Gameplay
    int runSeconds = 60;
    Color targetColor = Color{80, 220, 255, 255};
    float mapBrightness = 1.0f;  // 0.2 .. 1.6

    // Audio
    float masterVolume = 0.8f;
    float hitVolume = 0.7f;

    Keybinds keys = {};

    Crosshair crosshair;

    Config();
    // Missing files/keys simply keep defaults. Returns false if no file existed.
    bool Load(const std::string& path);
    bool Save(const std::string& path) const;
};
