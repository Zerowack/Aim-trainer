// app.h - Application state machine: menus, runs, sens finder, settings.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "camera.h"
#include "config.h"
#include "effects.h"
#include "modes.h"
#include "platform.h"
#include "rank.h"
#include "rng.h"
#include "sens_finder.h"
#include "stats.h"
#include "world.h"

constexpr const char* kAppVersion = "2.0.1";

// Game clock = real time minus all time spent paused.
struct GameClock {
    double pausedTotal = 0.0;
    double pauseStart = -1.0;

    void Pause(double now) {
        if (pauseStart < 0.0) pauseStart = now;
    }
    void Resume(double now) {
        if (pauseStart >= 0.0) {
            pausedTotal += now - pauseStart;
            pauseStart = -1.0;
        }
    }
    double Game(double now) const {
        const double paused = pausedTotal + (pauseStart >= 0.0 ? now - pauseStart : 0.0);
        return now - paused;
    }
};

enum class Screen {
    FirstRun,
    MainMenu,
    Playing,
    Results,
    Settings,
    Stats,
    FinderIntro,
    FinderReady,
    FinderComfort,
    FinderRound,
    FinderFinal,
    Rank,
    Difficulty,
    SniperSelect,
    BotSelect
};

class App {
public:
    int Run();

private:
    // --- lifecycle (app.cpp)
    bool Init();
    void Shutdown();
    void Frame();
    void ApplyDisplayMode();
    void ApplyAudio();
    void SaveConfig();
    std::string DataDir() const;
    double ActiveSens() const;
    void SetSens(double sens);
    void SyncSensText();

    // --- runs (app.cpp)
    void StartRun(ModeId id, bool finderTest);
    void BeginLive(double gameTime);
    void EndRun();
    void AbortRun();
    void Pause();
    void Resume();
    void UpdatePlaying(const std::vector<platform::RawEvent>& events, double now);
    void DrawPlaying();
    void DrawHud(double gameTime);
    void DrawPauseMenu();
    void DrawHitFeedback(double gameTime);

    // --- screens (app_screens.cpp)
    void ScreenFirstRun();
    void ScreenMainMenu();
    void ScreenResults();
    void ScreenSettings();
    void SettingsSensitivity(float x, float y, float w);
    void SettingsVideo(float x, float y, float w);
    void SettingsGameplay(float x, float y, float w);
    void SettingsCrosshair(float x, float y, float w);
    void SettingsAudio(float x, float y, float w);
    void SettingsKeybinds(float x, float y, float w);
    void ScreenStats();
    void ScreenFinderIntro();
    void ScreenFinderReady();
    void ScreenFinderComfort();
    void ScreenFinderRound();
    void ScreenFinderFinal();
    void ScreenRank();
    void ScreenDifficulty();
    void ScreenSniperSelect();
    void ScreenBotSelect();
    // Opens the right picker for a mode (rifle / bot rank / difficulty).
    void OpenModeSetup(ModeId mode);
    // Opens the difficulty picker for a mode (then starts the run).
    void ChooseDifficulty(ModeId mode);
    // Roast or neutral description, depending on the setting.
    const char* RankComment(const AimRank& r, unsigned int seed) const;
    void OpenSettings(Screen returnTo);
    void DrawSensSummary(float x, float y, float w, double dpi, double sens);
    void DrawFpsCounter();

    // --- core objects
    Config cfg_;
    StatsStore stats_;
    Audio audio_;
    World world_;
    ValCamera cam_;
    AimHistory history_;
    Rng rng_;
    std::unique_ptr<Mode> mode_;
    Effects fx_;

    // --- run state
    Screen screen_ = Screen::MainMenu;
    ModeId currentMode_ = ModeId::Gridshot;
    ModeId pickMode_ = ModeId::Gridshot;           // mode waiting for a difficulty
    ::Difficulty currentDifficulty_ = ::Difficulty::Normal;
    bool finderRun_ = false;
    bool live_ = false;
    bool paused_ = false;
    GameClock clock_;
    double countdownEnd_ = 0.0;
    double runStart_ = 0.0;
    double runLength_ = 60.0;
    double lastGameTime_ = 0.0;
    int lastBeep_ = -1;
    bool triggerHeld_ = false;
    long long frameCounter_ = 0;
    long long pauseFrame_ = -1;

    // --- results
    RunStats lastStats_;
    std::vector<std::string> lastTips_;
    bool lastWasPb_ = false;
    long long previousBest_ = 0;
    bool autoSensApplied_ = false;   // coach changed the sens after the last run
    double autoSensBefore_ = 0.0;
    int autoSensPct_ = 0;
    bool hadPreviousBest_ = false;
    bool lastRunRanked_ = false;
    AimRank lastRunRank_;
    bool lastModeRanked_ = false;
    AimRank lastModeRank_;   // for the score count-up animation

    // --- sens finder
    SensFinder finder_;
    std::vector<FinderSession> finderSessions_;
    RunStats finderLastStats_;
    bool finderSaved_ = false;
    FinderSession finderLastSession_;

    // --- settings / UI state
    Screen settingsReturn_ = Screen::MainMenu;
    int settingsTab_ = 0;
    int rebinding_ = -1;             // index of the keybind being captured
    long long rebindStartFrame_ = 0;
    std::string dpiText_;
    std::string sensText_;
    std::string customFpsText_;
    std::string scopedMultText_;
    std::string xhCodeText_;
    std::string xhMessage_;
    int statsMode_ = 0;

    // --- performance display / frame pacing
    double lastFrameTime_ = 0.0;
    double fpsWindowStart_ = 0.0;
    int fpsFrames_ = 0;
    double fpsShown_ = 0.0;
    double frameMsShown_ = 0.0;
    double worstMsWindow_ = 0.0;
    double worstMsShown_ = 0.0;
    double nextFrameDeadline_ = 0.0;

    // --- misc
    bool selfTestRan_ = false;
    bool selfTestOk_ = true;
    std::string selfTestReport_;
    bool rawInputOk_ = true;
    bool quit_ = false;
};
