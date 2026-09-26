// app.cpp - main loop, display modes, run flow, in-game HUD and pause menu.
#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "crosshair.h"
#include "input.h"
#include "selftest.h"
#include "ui.h"

namespace {

constexpr double kCountdownSeconds = 3.0;
constexpr int kMenuFpsLimit = 240;  // menus only; gameplay follows the FPS cap setting

std::string Trimmed(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s;
}

}  // namespace

// ===========================================================================
// Lifecycle

int App::Run() {
    if (!Init()) return 1;
    while (!quit_ && !WindowShouldClose()) Frame();
    Shutdown();
    return 0;
}

std::string App::DataDir() const { return GetApplicationDirectory(); }

bool App::Init() {
    // Settings first: they decide the window mode.
    const bool hadConfig = cfg_.Load(DataDir() + "config.ini");
    if (!hadConfig) cfg_.firstRunDone = false;

    platform::EnableDpiAwareness();
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);  // no FLAG_VSYNC_HINT: vsync stays off
    InitWindow(cfg_.windowWidth, cfg_.windowHeight, "RawAim - Valorant-calibrated aim trainer");
    if (!IsWindowReady()) return false;
    SetExitKey(KEY_NULL);  // Esc pauses instead of quitting

    rawInputOk_ = platform::Init(GetWindowHandle());
    if (!rawInputOk_) {
        platform::ShowErrorBox("RawAim", "Could not register for Raw Input. Mouse movement will not work.");
    }

    ApplyDisplayMode();
    ui::Init();
    world_.Init();
    audio_.Init();
    ApplyAudio();

    stats_.Load(DataDir() + "stats.csv");
    finderSessions_ = LoadFinderSessions(DataDir());
    SyncSensText();
    xhCodeText_ = EncodeCrosshair(cfg_.crosshair);

#ifndef NDEBUG
    // Debug builds verify the sensitivity / cm360 / FOV math at startup.
    selfTestRan_ = true;
    selfTestOk_ = RunSelfTest(selfTestReport_);
    TraceLog(selfTestOk_ ? LOG_INFO : LOG_ERROR, "%s", selfTestReport_.c_str());
    if (!selfTestOk_) platform::ShowErrorBox("RawAim self-test FAILED", selfTestReport_.c_str());
#endif

    screen_ = cfg_.firstRunDone ? Screen::MainMenu : Screen::FirstRun;
    lastFrameTime_ = platform::Now();
    fpsWindowStart_ = lastFrameTime_;
    return true;
}

void App::Shutdown() {
    mode_.reset();
    SaveConfig();
    platform::SetCursorLocked(false);
    audio_.Shutdown();
    world_.Shutdown();
    ui::Shutdown();
    platform::Shutdown();
    CloseWindow();
}

void App::SaveConfig() { cfg_.Save(DataDir() + "config.ini"); }

void App::ApplyAudio() { audio_.SetVolumes(cfg_.masterVolume, cfg_.hitVolume); }

void App::ApplyDisplayMode() {
    // Return to a plain window first, then switch to the requested mode.
    if (IsWindowFullscreen()) ToggleFullscreen();
    if (IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE)) ToggleBorderlessWindowed();

    const int monitor = GetCurrentMonitor();
    const int mw = GetMonitorWidth(monitor);
    const int mh = GetMonitorHeight(monitor);
    const Vector2 mpos = GetMonitorPosition(monitor);

    switch (cfg_.displayMode) {
        case DisplayMode::Fullscreen:
            // Exclusive fullscreen at the monitor's native resolution.
            SetWindowSize(mw, mh);
            ToggleFullscreen();
            break;
        case DisplayMode::Borderless:
            ToggleBorderlessWindowed();
            break;
        case DisplayMode::Windowed: {
            const int w = std::min(cfg_.windowWidth, mw - 80);
            const int h = std::min(cfg_.windowHeight, mh - 120);
            SetWindowSize(w, h);
            SetWindowPosition(static_cast<int>(mpos.x) + (mw - w) / 2, static_cast<int>(mpos.y) + (mh - h) / 2);
            break;
        }
    }
    platform::SetVSync(false);  // some drivers re-enable it after a mode change
    nextFrameDeadline_ = 0.0;
}

double App::ActiveSens() const { return finderRun_ ? finder_.CurrentSens() : cfg_.sens; }

void App::SetSens(double sens) {
    // Valorant accepts 3 decimals for sensitivity.
    cfg_.sens = std::max(0.001, std::round(sens * 1000.0) / 1000.0);
    SyncSensText();
    SaveConfig();
}

void App::SyncSensText() {
    dpiText_ = Trimmed(cfg_.dpi, 0);
    customFpsText_ = std::to_string(cfg_.customFpsCap);
    sensText_ = Trimmed(cfg_.sens, 4);
}

// ===========================================================================
// Frame

void App::Frame() {
    ++frameCounter_;

    // 1) Fresh input: dispatch all pending window messages (WM_INPUT, keys).
    platform::PumpMessages();
    if (platform::QuitRequested()) quit_ = true;

    const double now = platform::Now();
    const double frameMs = (now - lastFrameTime_) * 1000.0;
    lastFrameTime_ = now;

    // FPS / frame time readout, refreshed twice per second.
    ++fpsFrames_;
    worstMsWindow_ = std::max(worstMsWindow_, frameMs);
    if (now - fpsWindowStart_ >= 0.5) {
        fpsShown_ = fpsFrames_ / (now - fpsWindowStart_);
        frameMsShown_ = (now - fpsWindowStart_) * 1000.0 / fpsFrames_;
        worstMsShown_ = worstMsWindow_;
        fpsFrames_ = 0;
        worstMsWindow_ = 0.0;
        fpsWindowStart_ = now;
    }

    // 2) Focus loss (alt-tab, Windows key, popups): pause and free the cursor.
    if (platform::ConsumeFocusLost() || !platform::HasFocus()) {
        if (screen_ == Screen::Playing && !paused_) Pause();
    }

    std::vector<platform::RawEvent> events;
    platform::TakeRawEvents(events);

    ui::BeginFrame();

    if (!ui::AnyTextBoxFocused() && rebinding_ < 0 && input::BindPressed(cfg_.keys.toggleFps)) {
        cfg_.showFps = !cfg_.showFps;
    }

    // 3) Cursor lock: only while actually playing.
    const bool wantLock = screen_ == Screen::Playing && !paused_ && platform::HasFocus();
    if (wantLock != platform::IsCursorLocked()) {
        platform::SetCursorLocked(wantLock);
        if (wantLock) {
            HideCursor();
            events.clear();  // never apply movement made while the cursor was free
            triggerHeld_ = false;
        } else {
            ShowCursor();
        }
    }
    platform::UpdateCursorLock();

    // 4) Game logic.
    if (screen_ == Screen::Playing) UpdatePlaying(events, now);

    // 5) Render (+ immediate-mode UI for menus).
    BeginDrawing();
    switch (screen_) {
        case Screen::Playing: DrawPlaying(); break;
        case Screen::FirstRun: ui::Backdrop(); ScreenFirstRun(); break;
        case Screen::MainMenu: ui::Backdrop(); ScreenMainMenu(); break;
        case Screen::Results: ui::Backdrop(); ScreenResults(); break;
        case Screen::Settings: ui::Backdrop(); ScreenSettings(); break;
        case Screen::Stats: ui::Backdrop(); ScreenStats(); break;
        case Screen::FinderIntro: ui::Backdrop(); ScreenFinderIntro(); break;
        case Screen::FinderReady: ui::Backdrop(); ScreenFinderReady(); break;
        case Screen::FinderComfort: ui::Backdrop(); ScreenFinderComfort(); break;
        case Screen::FinderRound: ui::Backdrop(); ScreenFinderRound(); break;
        case Screen::FinderFinal: ui::Backdrop(); ScreenFinderFinal(); break;
    }
    if (screen_ != Screen::Playing) DrawFpsCounter();
    EndDrawing();

    // 6) Targets drawn for the first time become "visible" now, after the
    //    buffer swap, so reaction time starts when they could be seen.
    if (screen_ == Screen::Playing && live_ && !paused_ && mode_) mode_->OnPresented(clock_.Game(platform::Now()));

    // 7) Optional frame cap (precise sleep + spin, no vsync).
    int cap = cfg_.FpsCap();
    if (screen_ != Screen::Playing && (cap == 0 || cap > kMenuFpsLimit)) cap = kMenuFpsLimit;
    if (IsWindowMinimized()) cap = 30;
    if (cap > 0) {
        const double period = 1.0 / cap;
        const double t = platform::Now();
        if (nextFrameDeadline_ <= 0.0 || t - nextFrameDeadline_ > period) nextFrameDeadline_ = t + period;
        else nextFrameDeadline_ += period;
        platform::PreciseWaitUntil(nextFrameDeadline_);
    } else {
        nextFrameDeadline_ = 0.0;
    }
}

// ===========================================================================
// Runs

void App::StartRun(ModeId id, bool finderTest) {
    GameContext ctx;
    ctx.cam = &cam_;
    ctx.history = &history_;
    ctx.world = &world_;
    ctx.audio = &audio_;
    ctx.rng = &rng_;
    ctx.targetColor = cfg_.targetColor;
    ctx.brightness = cfg_.mapBrightness;

    mode_.reset();  // destroy the old mode first (it may own world covers)
    mode_ = CreateMode(id, ctx);
    currentMode_ = id;
    finderRun_ = finderTest;
    cam_.Reset(0.0, 0.0);
    history_.Clear();
    triggerHeld_ = false;

    clock_ = GameClock{};
    const double g = clock_.Game(platform::Now());
    countdownEnd_ = g + kCountdownSeconds;
    lastGameTime_ = g;
    lastBeep_ = -1;
    live_ = false;
    paused_ = false;
    runLength_ = finderTest ? SensFinder::kTestSeconds : static_cast<double>(cfg_.runSeconds);
    screen_ = Screen::Playing;
    ui::ClearFocus();
}

void App::BeginLive(double gameTime) {
    live_ = true;
    runStart_ = gameTime;
    mode_->Begin(gameTime);
    audio_.Play(Sfx::CountGo);
}

void App::EndRun() {
    RunStats s = mode_->Stats();
    s.duration = runLength_;
    if (s.score < 0) s.score = 0;
    mode_.reset();
    live_ = false;

    if (finderRun_) {
        finderLastStats_ = s;
        screen_ = Screen::FinderComfort;
        return;
    }

    hadPreviousBest_ = stats_.BestScore(s.mode, previousBest_);
    lastWasPb_ = !hadPreviousBest_ || s.score > previousBest_;
    stats_.Append(MakeRecord(s, cfg_.sens, cfg_.dpi));
    lastStats_ = s;
    lastTips_ = BuildTips(s, cfg_.sens);

    // Optional: let the coach apply its over/undershoot sens suggestion.
    autoSensApplied_ = false;
    const int pct = SuggestedSensChangePct(s);
    if (cfg_.autoSens && pct != 0) {
        autoSensBefore_ = cfg_.sens;
        autoSensPct_ = pct;
        SetSens(cfg_.sens * (1.0 + pct / 100.0));
        autoSensApplied_ = std::fabs(cfg_.sens - autoSensBefore_) > 1e-9;
    }
    screen_ = Screen::Results;
}

void App::AbortRun() {
    mode_.reset();
    live_ = false;
    paused_ = false;
    if (finderRun_) {
        finderRun_ = false;
        finder_.Cancel();
        screen_ = Screen::FinderIntro;
    } else {
        screen_ = Screen::MainMenu;
    }
}

void App::Pause() {
    if (paused_) return;
    paused_ = true;
    clock_.Pause(platform::Now());
    triggerHeld_ = false;
    pauseFrame_ = frameCounter_;
}

void App::Resume() {
    if (!paused_) return;
    paused_ = false;
    clock_.Resume(platform::Now());
    history_.Clear();
    ui::ClearFocus();
}

void App::UpdatePlaying(const std::vector<platform::RawEvent>& events, double now) {
    if (paused_ || !mode_) return;

    if (IsKeyPressed(KEY_ESCAPE) || input::BindPressed(cfg_.keys.pause)) {
        Pause();
        return;
    }
    if (!finderRun_ && input::BindPressed(cfg_.keys.restart)) {
        StartRun(currentMode_, false);
        return;
    }

    const double g = clock_.Game(now);
    const double dt = std::min(0.05, std::max(0.0, g - lastGameTime_));  // clamp hitches
    lastGameTime_ = g;

    // Apply every raw mouse packet in order; clicks fire at the exact
    // orientation they happened at.
    Mode* mode = mode_.get();
    const bool live = live_;
    input::ProcessRawStream(
        events, cam_, history_, ActiveSens(), cfg_.keys.shoot, triggerHeld_,
        [this](double t) { return clock_.Game(t); },
        [mode, live](double t) {
            if (live) mode->OnShot(t);
        });

    // Keyboard shoot bind (frame precision).
    if (!input::IsMouseBind(cfg_.keys.shoot)) {
        const bool down = IsKeyDown(cfg_.keys.shoot);
        if (down && !triggerHeld_ && live_) mode_->OnShot(g);
        triggerHeld_ = down;
    }

    if (!live_) {
        const double remaining = countdownEnd_ - g;
        const int sec = static_cast<int>(std::ceil(remaining));
        if (sec != lastBeep_ && sec > 0) {
            lastBeep_ = sec;
            audio_.Play(Sfx::CountBeep);
        }
        if (remaining <= 0.0) BeginLive(g);
        return;
    }

    mode_->Update(g, dt, triggerHeld_);
    if (g - runStart_ >= runLength_) EndRun();
}

// ===========================================================================
// In-game drawing

void App::DrawPlaying() {
    const float b = cfg_.mapBrightness;
    ClearBackground(Shade(Color{28, 31, 38, 255}, b));

    BeginMode3D(cam_.ToRaylib());
    world_.SetViewPosition(cam_.Eye());
    world_.DrawRange(b);
    if (mode_) mode_->Draw3D();
    EndMode3D();

    const double g = clock_.Game(platform::Now());
    DrawHud(g);
    DrawCrosshair(cfg_.crosshair, GetScreenWidth() / 2, GetScreenHeight() / 2);
    if (paused_) DrawPauseMenu();
}

void App::DrawFpsCounter() {
    if (!cfg_.showFps) return;
    const std::string s = TextFormat("%.0f FPS  %.2f ms  (max %.1f)", fpsShown_, frameMsShown_, worstMsShown_);
    ui::Fill(Rectangle{12.0f, 12.0f, ui::TextWidth(s, 18.0f) + 20.0f, 30.0f}, ui::Alpha(BLACK, 0.45f));
    ui::Text(s, 22.0f, 17.0f, 18.0f, ui::theme::kGood);
}

void App::DrawHud(double g) {
    using namespace ui;
    const float vw = VW(), vh = VH();

    DrawFpsCounter();

    if (mode_) {
        const RunStats& s = mode_->Stats();
        // Timer
        const double left = live_ ? std::max(0.0, runLength_ - (g - runStart_)) : runLength_;
        const int secs = static_cast<int>(std::ceil(left));
        Angled(Rectangle{vw * 0.5f - 90.0f, 14.0f, 180.0f, 64.0f}, Alpha(BLACK, 0.5f), 12.0f);
        Text(TextFormat("%d:%02d", secs / 60, secs % 60), vw * 0.5f, 20.0f, 40.0f, theme::kText, Align::Center);
        Text(ModeName(currentMode_), vw * 0.5f, 84.0f, 18.0f, Alpha(theme::kTextDim, 0.9f), Align::Center);

        // Score block (top right)
        const float rx = vw - 300.0f;
        Angled(Rectangle{rx, 14.0f, 286.0f, 112.0f}, Alpha(BLACK, 0.5f), 12.0f);
        Text(TextFormat("%lld", std::max<long long>(0, s.score)), rx + 270.0f, 20.0f, 40.0f, theme::kText, Align::Right);
        Text("SCORE", rx + 16.0f, 32.0f, 18.0f, theme::kAccent);
        if (currentMode_ == ModeId::Tracking) {
            Text(TextFormat("ON TARGET %.0f%%", (s.TrackingPct() > 0 ? s.TrackingPct() : 0.0) * 100.0), rx + 16.0f, 78.0f,
                 20.0f, theme::kTextDim);
        } else {
            Text(TextFormat("ACC %.0f%%   %d / %d", s.Accuracy() * 100.0, s.hits, s.Misses()), rx + 16.0f, 78.0f, 20.0f,
                 theme::kTextDim);
        }

        if (live_) mode_->DrawHud(g);

        if (!live_) {
            const int c = static_cast<int>(std::ceil(countdownEnd_ - g));
            Text(TextFormat("%d", std::max(1, c)), vw * 0.5f, vh * 0.5f - 150.0f, 110.0f, theme::kAccent, Align::Center);
            Text(finderRun_ ? TextFormat("TEST %c - GET READY", finder_.CurrentLabel()) : "GET READY", vw * 0.5f,
                 vh * 0.5f + 60.0f, 24.0f, theme::kText, Align::Center);
        }
    }

    if (cfg_.showFovInfo) {
        const double aspect = static_cast<double>(GetScreenWidth()) / std::max(1, GetScreenHeight());
        const double sens = ActiveSens();
        std::string info = TextFormat("HFOV %.1f  VFOV %.1f  |  %dx%d", val::GameHorizontalFov(aspect), val::GameVerticalFov(),
                                      GetScreenWidth(), GetScreenHeight());
        if (finderRun_) {
            info += "  |  SENS HIDDEN (A/B TEST)";
        } else {
            info += TextFormat("  |  SENS %.3f  |  %.0f DPI  |  eDPI %.0f  |  %.1f cm/360", sens, cfg_.dpi,
                               val::Edpi(cfg_.dpi, sens), val::Cm360(cfg_.dpi, sens));
        }
        Fill(Rectangle{12.0f, vh - 42.0f, TextWidth(info, 18.0f) + 20.0f, 30.0f}, Alpha(BLACK, 0.45f));
        Text(info, 22.0f, vh - 37.0f, 18.0f, theme::kTextDim);
    }
}

void App::DrawPauseMenu() {
    using namespace ui;
    const float vw = VW(), vh = VH();
    Fill(Rectangle{0.0f, 0.0f, vw, vh}, Alpha(theme::kBg, 0.78f));
    const Rectangle panel = {vw * 0.5f - 260.0f, vh * 0.5f - 250.0f, 520.0f, 500.0f};
    Angled(panel, theme::kPanel, 22.0f);
    Title("PAUSED", panel.x + 40.0f, panel.y + 36.0f, 44.0f);

    const float bx = panel.x + 60.0f, bw = panel.width - 120.0f;
    float y = panel.y + 130.0f;
    const bool escNow = IsKeyPressed(KEY_ESCAPE) && pauseFrame_ != frameCounter_;
    if (Button(Rectangle{bx, y, bw, 58.0f}, "RESUME", true) || escNow) {
        Resume();
        return;
    }
    y += 76.0f;
    if (Button(Rectangle{bx, y, bw, 58.0f}, finderRun_ ? "RESTART TEST" : "RESTART")) {
        StartRun(currentMode_, finderRun_);
        return;
    }
    y += 76.0f;
    if (Button(Rectangle{bx, y, bw, 58.0f}, "SETTINGS")) {
        OpenSettings(Screen::Playing);
        return;
    }
    y += 76.0f;
    if (Button(Rectangle{bx, y, bw, 58.0f}, finderRun_ ? "ABORT SENS FINDER" : "QUIT TO MENU")) {
        AbortRun();
        return;
    }
    Text("Esc to resume", panel.x + panel.width * 0.5f, panel.y + panel.height - 40.0f, 18.0f, theme::kTextDim,
         Align::Center);
}
