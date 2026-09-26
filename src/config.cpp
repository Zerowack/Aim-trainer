// config.cpp - Simple "key=value" config file.
#include "config.h"

#include <cstdlib>
#include <fstream>
#include <map>

#include "input.h"

const int kFpsCapValues[kFpsCapCount] = {0, 144, 240, 360, 0};
const char* const kFpsCapNames[kFpsCapCount] = {"Uncapped", "144", "240", "360", "Custom"};

namespace {

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

double GetD(const std::map<std::string, std::string>& kv, const char* key, double def) {
    auto it = kv.find(key);
    if (it == kv.end()) return def;
    char* end = nullptr;
    const double v = std::strtod(it->second.c_str(), &end);
    return (end == it->second.c_str()) ? def : v;
}

int GetI(const std::map<std::string, std::string>& kv, const char* key, int def) {
    return static_cast<int>(GetD(kv, key, static_cast<double>(def)));
}

float GetF(const std::map<std::string, std::string>& kv, const char* key, float def) {
    return static_cast<float>(GetD(kv, key, static_cast<double>(def)));
}

bool GetB(const std::map<std::string, std::string>& kv, const char* key, bool def) {
    return GetI(kv, key, def ? 1 : 0) != 0;
}

double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
int ClampI(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

unsigned char ToByte(int v) { return static_cast<unsigned char>(ClampI(v, 0, 255)); }

}  // namespace

Config::Config() {
    keys.shoot = input::MouseBind(0);  // left mouse button
    keys.restart = KEY_R;
    keys.pause = KEY_P;
    keys.toggleFps = KEY_F2;
    keys.scope = input::MouseBind(1);  // right mouse button
    keys.forward = KEY_W;
    keys.back = KEY_S;
    keys.left = KEY_A;
    keys.right = KEY_D;
    keys.walk = KEY_LEFT_SHIFT;
    keys.crouch = KEY_LEFT_CONTROL;
    keys.jump = KEY_SPACE;
    keys.reload = KEY_R;
    keys.screenshot = KEY_F12;
}

int Config::FpsCap() const {
    if (fpsCapIndex == kFpsCapCustomIndex) return ClampI(customFpsCap, kCustomFpsMin, kCustomFpsMax);
    return kFpsCapValues[ClampI(fpsCapIndex, 0, kFpsCapCount - 1)];
}

bool Config::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }

    firstRunDone = GetB(kv, "first_run_done", firstRunDone);
    dpi = ClampD(GetD(kv, "dpi", dpi), 50.0, 32000.0);
    sens = ClampD(GetD(kv, "sens", sens), 0.001, 20.0);
    autoSens = GetB(kv, "auto_sens", autoSens);

    displayMode = static_cast<DisplayMode>(ClampI(GetI(kv, "display_mode", static_cast<int>(displayMode)), 0, 2));
    // One-time move from exclusive fullscreen (the old default) to borderless,
    // which streams, screenshots and Alt-Tabs properly. Pick Fullscreen again
    // in Settings > Video to keep it.
    if (kv.find("display_v2") == kv.end() && displayMode == DisplayMode::Fullscreen) displayMode = DisplayMode::Borderless;
    windowWidth = ClampI(GetI(kv, "window_width", windowWidth), 640, 7680);
    windowHeight = ClampI(GetI(kv, "window_height", windowHeight), 360, 4320);
    fpsCapIndex = ClampI(GetI(kv, "fps_cap_index", fpsCapIndex), 0, kFpsCapCount - 1);
    customFpsCap = ClampI(GetI(kv, "custom_fps_cap", customFpsCap), kCustomFpsMin, kCustomFpsMax);
    showFps = GetB(kv, "show_fps", showFps);
    showFovInfo = GetB(kv, "show_fov_info", showFovInfo);

    runSeconds = ClampI(GetI(kv, "run_seconds", runSeconds), 10, 600);
    targetColor.r = ToByte(GetI(kv, "target_r", targetColor.r));
    targetColor.g = ToByte(GetI(kv, "target_g", targetColor.g));
    targetColor.b = ToByte(GetI(kv, "target_b", targetColor.b));
    mapBrightness = ClampF(GetF(kv, "map_brightness", mapBrightness), 0.2f, 1.6f);
    roastMode = GetB(kv, "roast_mode", roastMode);
    difficulty = ClampI(GetI(kv, "difficulty", difficulty), 0, 3);

    masterVolume = ClampF(GetF(kv, "master_volume", masterVolume), 0.0f, 1.0f);
    hitVolume = ClampF(GetF(kv, "hit_volume", hitVolume), 0.0f, 1.0f);

    keys.shoot = GetI(kv, "key_shoot", keys.shoot);
    keys.restart = GetI(kv, "key_restart", keys.restart);
    keys.pause = GetI(kv, "key_pause", keys.pause);
    keys.toggleFps = GetI(kv, "key_toggle_fps", keys.toggleFps);
    keys.scope = GetI(kv, "key_scope", keys.scope);
    keys.forward = GetI(kv, "key_forward", keys.forward);
    keys.back = GetI(kv, "key_back", keys.back);
    keys.left = GetI(kv, "key_left", keys.left);
    keys.right = GetI(kv, "key_right", keys.right);
    keys.walk = GetI(kv, "key_walk", keys.walk);
    keys.crouch = GetI(kv, "key_crouch", keys.crouch);
    keys.jump = GetI(kv, "key_jump", keys.jump);
    keys.reload = GetI(kv, "key_reload", keys.reload);
    keys.screenshot = GetI(kv, "key_screenshot", keys.screenshot);
    scopedMult = ClampD(GetD(kv, "scoped_sens_multiplier", scopedMult), 0.01, 10.0);
    sniperWeapon = ClampI(GetI(kv, "sniper_weapon", sniperWeapon), 0, 2);
    botTier = ClampI(GetI(kv, "bot_tier", botTier), 0, 8);
    scopeHold = GetB(kv, "scope_hold", scopeHold);

    auto it = kv.find("crosshair");
    if (it != kv.end()) {
        Crosshair c;
        if (DecodeCrosshair(it->second, c, nullptr)) crosshair = c;
    }
    return true;
}

bool Config::Save(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out.precision(8);
    out << "# Aim trainer settings. Edit in the app, or here while the app is closed.\n";
    out << "first_run_done=" << (firstRunDone ? 1 : 0) << "\n";
    out << "dpi=" << dpi << "\n";
    out << "sens=" << sens << "\n";
    out << "auto_sens=" << (autoSens ? 1 : 0) << "\n";
    out << "display_mode=" << static_cast<int>(displayMode) << "   # 0=fullscreen 1=borderless 2=windowed\n";
    out << "display_v2=1\n";
    out << "window_width=" << windowWidth << "\n";
    out << "window_height=" << windowHeight << "\n";
    out << "fps_cap_index=" << fpsCapIndex << "   # 0=uncapped 1=144 2=240 3=360 4=custom\n";
    out << "custom_fps_cap=" << customFpsCap << "\n";
    out << "show_fps=" << (showFps ? 1 : 0) << "\n";
    out << "show_fov_info=" << (showFovInfo ? 1 : 0) << "\n";
    out << "run_seconds=" << runSeconds << "\n";
    out << "target_r=" << static_cast<int>(targetColor.r) << "\n";
    out << "target_g=" << static_cast<int>(targetColor.g) << "\n";
    out << "target_b=" << static_cast<int>(targetColor.b) << "\n";
    out << "map_brightness=" << mapBrightness << "\n";
    out << "roast_mode=" << (roastMode ? 1 : 0) << "\n";
    out << "difficulty=" << difficulty << "   # 0=easy 1=normal 2=hard 3=insane\n";
    out << "master_volume=" << masterVolume << "\n";
    out << "hit_volume=" << hitVolume << "\n";
    out << "key_shoot=" << keys.shoot << "\n";
    out << "key_restart=" << keys.restart << "\n";
    out << "key_pause=" << keys.pause << "\n";
    out << "key_toggle_fps=" << keys.toggleFps << "\n";
    out << "key_scope=" << keys.scope << "\n";
    out << "key_forward=" << keys.forward << "\n";
    out << "key_back=" << keys.back << "\n";
    out << "key_left=" << keys.left << "\n";
    out << "key_right=" << keys.right << "\n";
    out << "key_walk=" << keys.walk << "\n";
    out << "key_crouch=" << keys.crouch << "\n";
    out << "key_jump=" << keys.jump << "\n";
    out << "key_reload=" << keys.reload << "\n";
    out << "key_screenshot=" << keys.screenshot << "\n";
    out << "scoped_sens_multiplier=" << scopedMult << "\n";
    out << "sniper_weapon=" << sniperWeapon << "   # 0=marshal 1=outlaw 2=operator\n";
    out << "scope_hold=" << (scopeHold ? 1 : 0) << "\n";
    out << "bot_tier=" << botTier << "   # VS Bot: 0=iron .. 8=radiant\n";
    out << "crosshair=" << EncodeCrosshair(crosshair) << "\n";
    return static_cast<bool>(out);
}
