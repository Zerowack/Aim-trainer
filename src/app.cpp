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
    InitWindow(cfg_.windowWidth, cfg_.windowHeight, "Valtrainer - Valorant-calibrated aim trainer");
    if (!IsWindowReady()) return false;
    SetExitKey(KEY_NULL);  // Esc pauses instead of quitting

    rawInputOk_ = platform::Init(GetWindowHandle());
    if (!rawInputOk_) {
        platform::ShowErrorBox("Valtrainer", "Could not register for Raw Input. Mouse movement will not work.");
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
    if (!selfTestOk_) platform::ShowErrorBox("Valtrainer self-test FAILED", selfTestReport_.c_str());
#endif

    screen_ = cfg_.firstRunDone ? Screen::MainMenu : Screen::FirstRun;
    currentDifficulty_ = static_cast<::Difficulty>(cfg_.difficulty);
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
            // raylib makes borderless windows "always on top", which stops
            // Alt-Tab / the Windows key from showing other programs.
            ClearWindowState(FLAG_WINDOW_TOPMOST);
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
    scopedMultText_ = Trimmed(cfg_.scopedMult, 3);
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

    // While minimized: draw nothing (the window has no size), just idle.
    if (IsWindowMinimized()) {
        BeginDrawing();
        EndDrawing();
        platform::PreciseWaitUntil(platform::Now() + 1.0 / 30.0);
        return;
    }

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
        case Screen::Rank: ui::Backdrop(); ScreenRank(); break;
        case Screen::Difficulty: ui::Backdrop(); ScreenDifficulty(); break;
        case Screen::SniperSelect: ui::Backdrop(); ScreenSniperSelect(); break;
        case Screen::BotSelect: ui::Backdrop(); ScreenBotSelect(); break;
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
    ctx.fx = &fx_;
    ctx.targetColor = cfg_.targetColor;
    ctx.brightness = cfg_.mapBrightness;
    // The sens finder test always runs at Normal so its scores are comparable.
    ctx.difficulty = finderTest ? ::Difficulty::Normal : currentDifficulty_;
    ctx.diff = GetDifficulty(ctx.difficulty);
    ctx.weapon = static_cast<SniperWeapon>(cfg_.sniperWeapon);
    ctx.scopeBind = cfg_.keys.scope;
    ctx.scopeHold = cfg_.scopeHold;
    ctx.botTier = cfg_.botTier;

    mode_.reset();  // destroy the old mode first (it may own world covers)
    cam_.Reset(0.0, 0.0);  // before creating the mode: it may move the eye
    history_.Clear();
    fx_.Clear();
    mode_ = CreateMode(id, ctx);
    currentMode_ = id;
    finderRun_ = finderTest;
    triggerHeld_ = false;

    clock_ = GameClock{};
    const double g = clock_.Game(platform::Now());
    countdownEnd_ = g + kCountdownSeconds;
    lastGameTime_ = g;
    lastBeep_ = -1;
    live_ = false;
    paused_ = false;
    runLength_ = finderTest ? SensFinder::kTestSeconds : static_cast<double>(cfg_.runSeconds);
    if (id == ModeId::VsBot) runLength_ = 3600.0;  // the match ends itself (first to 5 rounds)
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
    s.duration = std::min(runLength_, std::max(0.0, lastGameTime_ - runStart_));
    if (s.score < 0) s.score = 0;
    mode_.reset();
    live_ = false;

    if (finderRun_) {
        finderLastStats_ = s;
        screen_ = Screen::FinderComfort;
        return;
    }

    hadPreviousBest_ = stats_.BestScore(s.mode, s.difficulty, previousBest_);
    lastWasPb_ = !hadPreviousBest_ || s.score > previousBest_;
    const RunRecord record = MakeRecord(s, cfg_.sens, cfg_.dpi);
    stats_.Append(record);
    lastRunRanked_ = RankFromRecord(record, lastRunRank_);
    lastModeRanked_ = ModeRank(stats_, s.mode, lastModeRank_);
    lastStats_ = s;
    resultsShownAt_ = platform::Now();
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
    // (VS Bot uses R to reload, so restart is only in the pause menu there.)
    if (!finderRun_ && currentMode_ != ModeId::VsBot && input::BindPressed(cfg_.keys.restart)) {
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
    // Sensitivity is re-read for every packet: scoping in mid-frame applies
    // the scoped sens (sens x multiplier / zoom) to exactly the packets after it.
    const double baseSens = ActiveSens();
    const double scopedMult = cfg_.scopedMult;
    input::ProcessRawStream(
        events, cam_, history_, [mode, baseSens, scopedMult]() { return val::ScopedSens(baseSens, scopedMult, mode->Zoom()); },
        cfg_.keys.shoot, triggerHeld_, [this](double t) { return clock_.Game(t); },
        [mode, live](double t) {
            if (live) mode->OnShot(t);
        },
        [mode, live](int bind, bool down, double t) {
            if (live) mode->OnButton(bind, down, t);
        });
    // Keyboard scope bind (frame precision).
    if (live_ && !input::IsMouseBind(cfg_.keys.scope) && cfg_.keys.scope > 0) {
        if (IsKeyPressed(cfg_.keys.scope)) mode_->OnButton(cfg_.keys.scope, true, g);
        if (IsKeyReleased(cfg_.keys.scope)) mode_->OnButton(cfg_.keys.scope, false, g);
    }

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
    fx_.Update(dt);
    if (g - runStart_ >= runLength_ || mode_->Finished()) EndRun();
}

// ===========================================================================
// In-game drawing

void App::DrawPlaying() {
    const float b = cfg_.mapBrightness;
    ClearBackground(world_.SkyColor(b));

    BeginMode3D(cam_.ToRaylib(mode_ ? mode_->Zoom() : 1.0));
    world_.BeginFrame(cam_.Eye(), b);
    world_.DrawRange();
    if (mode_) mode_->Draw3D();
    fx_.Draw(world_);
    EndMode3D();

    const double g = clock_.Game(platform::Now());
    if (mode_) mode_->DrawOverlay();
    DrawHud(g);
    if (!mode_ || !mode_->HideCrosshair()) DrawCrosshair(cfg_.crosshair, GetScreenWidth() / 2, GetScreenHeight() / 2);
    DrawHitFeedback(g);
    if (paused_) DrawPauseMenu();
}

void App::DrawFpsCounter() {
    if (!cfg_.showFps) return;
    const std::string s = TextFormat("%.0f FPS  %.2f ms  (max %.1f)", fpsShown_, frameMsShown_, worstMsShown_);
    ui::Fill(Rectangle{12.0f, 12.0f, ui::TextWidth(s, 18.0f) + 20.0f, 30.0f}, ui::Alpha(BLACK, 0.45f));
    ui::Text(s, 22.0f, 17.0f, 18.0f, ui::theme::kGood);
}

void App::DrawHitFeedback(double g) {
    if (!mode_ || !live_ || paused_) return;
    const ShotFeedback& fb = mode_->LastShot();
    const double dt = g - fb.time;
    if (!fb.hit || dt < 0.0 || dt > 0.14) return;
    // Hit marker: four short diagonal ticks around the crosshair.
    const float a = static_cast<float>(1.0 - dt / 0.14);
    const float s = std::max(1.0f, ui::Scale() * 1.4f);
    const float in = 8.0f * s, out = 15.0f * s, th = std::max(2.0f, 1.8f * s);
    const Vector2 c = {static_cast<float>(GetScreenWidth() / 2) + 0.5f, static_cast<float>(GetScreenHeight() / 2) + 0.5f};
    const Color col = fb.head ? Color{255, 90, 100, static_cast<unsigned char>(255 * a)}
                              : Color{255, 255, 255, static_cast<unsigned char>(230 * a)};
    for (int i = 0; i < 4; ++i) {
        const float dx = (i & 1) ? 1.0f : -1.0f, dy = (i & 2) ? 1.0f : -1.0f;
        const float k = 0.70710678f;
        DrawLineEx(Vector2{c.x + dx * in * k, c.y + dy * in * k}, Vector2{c.x + dx * out * k, c.y + dy * out * k}, th, col);
    }
}

void App::DrawHud(double g) {
    using namespace ui;
    const float vw = VW(), vh = VH();
    const float cx = vw * 0.5f, cy = vh * 0.5f;

    DrawFpsCounter();

    if (mode_) {
        const RunStats& s = mode_->Stats();

        // Timer with a progress bar; the last 5 seconds pulse red.
        double left = live_ ? std::max(0.0, runLength_ - (g - runStart_)) : runLength_;
        double timerTotal = runLength_;
        std::string timerLabel = ModeName(currentMode_);
        if (mode_->HudTimer(g, left, timerLabel)) timerTotal = 60.0;  // e.g. VS Bot round timer
        const int secs = static_cast<int>(std::ceil(left));
        const bool finalSeconds = live_ && left <= 5.0 && left > 0.0;
        const Rectangle tb = {cx - 110.0f, 14.0f, 220.0f, 74.0f};
        Angled(tb, Alpha(theme::kBg, 0.78f), 14.0f);
        const float frac = timerTotal > 0.0 ? static_cast<float>(std::min(1.0, left / timerTotal)) : 0.0f;
        Fill(Rectangle{tb.x + 16.0f, tb.y + tb.height - 12.0f, tb.width - 32.0f, 3.0f}, Alpha(theme::kLine, 0.8f));
        Fill(Rectangle{tb.x + 16.0f, tb.y + tb.height - 12.0f, (tb.width - 32.0f) * frac, 3.0f},
             finalSeconds ? theme::kAccent : theme::kText);
        const bool blink = finalSeconds && std::fmod(left, 1.0) > 0.5;
        TextBold(TextFormat("%d:%02d", secs / 60, secs % 60), cx, 20.0f, 40.0f, blink ? theme::kAccent : theme::kText,
                 Align::Center);
        Text(timerLabel, cx, 96.0f, 17.0f, Alpha(theme::kTextDim, 0.9f), Align::Center);

        // Score block (top right) with a red accent edge.
        const float rx = vw - 300.0f;
        const Rectangle sb = {rx, 14.0f, 286.0f, 112.0f};
        Angled(sb, Alpha(theme::kBg, 0.78f), 14.0f);
        Fill(Rectangle{sb.x, sb.y, 4.0f, sb.height - 14.0f}, theme::kAccent);
        TextBold(TextFormat("%lld", std::max<long long>(0, s.score)), rx + 270.0f, 20.0f, 40.0f, theme::kText, Align::Right);
        Text("SCORE", rx + 18.0f, 32.0f, 18.0f, theme::kAccent);
        if (currentMode_ == ModeId::Tracking) {
            Text(TextFormat("ON TARGET %.0f%%", (s.TrackingPct() > 0 ? s.TrackingPct() : 0.0) * 100.0), rx + 18.0f, 80.0f,
                 20.0f, theme::kTextDim);
        } else {
            Text(TextFormat("ACC %.0f%%   %d / %d", s.Accuracy() * 100.0, s.hits, s.Misses()), rx + 18.0f, 80.0f, 20.0f,
                 theme::kTextDim);
        }
        // Hit streak.
        const int streak = mode_->Streak();
        if (live_ && streak >= 3) {
            const Rectangle st = {rx + 126.0f, 134.0f, 160.0f, 36.0f};
            Angled(st, Alpha(theme::kAccent, 0.85f), 10.0f);
            TextBold(TextFormat("STREAK x%d", streak), st.x + st.width * 0.5f, st.y + 7.0f, 20.0f, theme::kText, Align::Center);
        }

        if (live_) {
            mode_->DrawHud(g);
            // Points pop-up that rises and fades next to the crosshair.
            const ShotFeedback& fb = mode_->LastShot();
            const double dt = g - fb.time;
            if (fb.hit && fb.points > 0 && dt >= 0.0 && dt < 0.6) {
                const float t = static_cast<float>(dt / 0.6);
                Text(TextFormat("+%lld", fb.points), cx + 46.0f, cy - 34.0f - 46.0f * t, 26.0f,
                     Alpha(fb.head ? theme::kWarn : theme::kText, 1.0f - t * t));
            }
        } else {
            // Countdown: each number starts big and settles.
            const double remaining = countdownEnd_ - g;
            const int c = std::max(1, static_cast<int>(std::ceil(remaining)));
            const float f = static_cast<float>(remaining - std::floor(remaining));  // 1 -> 0 within each second
            const float size = 110.0f * (1.0f + 0.3f * f * f);
            TextBold(TextFormat("%d", c), cx, cy - 150.0f - (size - 110.0f) * 0.5f, size, Alpha(theme::kAccent, 0.55f + 0.45f * (1.0f - f)),
                     Align::Center);
            Text(finderRun_ ? TextFormat("TEST %c - GET READY", finder_.CurrentLabel()) : "GET READY", cx, cy + 60.0f, 24.0f,
                 theme::kText, Align::Center);
        }
    }

    if (cfg_.showFovInfo) {
        const double aspect = static_cast<double>(GetScreenWidth()) / std::max(1, GetScreenHeight());
        const double zoom = mode_ ? mode_->Zoom() : 1.0;
        const double sens = ActiveSens();
        const double vfov = val::ZoomedVerticalFov(zoom);
        std::string info = TextFormat("HFOV %.1f  VFOV %.1f  |  %dx%d", val::HorizontalFovFromVertical(vfov, aspect), vfov,
                                      GetScreenWidth(), GetScreenHeight());
        if (finderRun_) {
            info += "  |  SENS HIDDEN (A/B TEST)";
        } else if (zoom > 1.0) {
            const double eff = val::ScopedSens(sens, cfg_.scopedMult, zoom);
            info += TextFormat("  |  SCOPED %.1fx  x%.2f  |  effective sens %.4f  |  %.1f cm/360", zoom, cfg_.scopedMult, eff,
                               val::Cm360(cfg_.dpi, eff));
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
    const Rectangle panel = {vw * 0.5f - 260.0f, vh * 0.5f - 290.0f, 520.0f, 580.0f};
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
    if (Button(Rectangle{bx, y, bw, 58.0f}, "MINIMIZE")) {
        MinimizeWindow();  // the run stays paused
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
