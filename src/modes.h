// modes.h - The training modes.
//
// Every mode works on the "game clock": seconds from platform::Now() with all
// paused time removed, so pausing never affects reaction time or TTK.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio.h"
#include "effects.h"
#include "camera.h"
#include "raylib.h"
#include "rng.h"
#include "stats.h"
#include "targets.h"
#include "world.h"

struct GameContext {
    ValCamera* cam = nullptr;
    AimHistory* history = nullptr;
    World* world = nullptr;
    Audio* audio = nullptr;
    Rng* rng = nullptr;
    Effects* fx = nullptr;  // optional cosmetic effects
    Color targetColor = Color{80, 220, 255, 255};
    float brightness = 1.0f;  // map brightness (targets are never dimmed)
    Difficulty difficulty = Difficulty::Normal;
    DifficultyParams diff = GetDifficulty(Difficulty::Normal);
    // Sniper mode
    SniperWeapon weapon = SniperWeapon::Operator;
    int scopeBind = 1001;   // input bind code (default Mouse 2)
    bool scopeHold = false; // hold to scope instead of toggle
    // VS Bot
    int botTier = 3;        // 0 Iron .. 8 Radiant
};

// Approximate Valorant sniper stats (zoom = magnification).
struct SniperSpec {
    const char* name;
    int magazine;
    double shotInterval;  // seconds between shots
    double reloadTime;    // seconds
    double zoom1;
    double zoom2;         // 0 = only one zoom level
    double hipSpreadDeg;  // unscoped inaccuracy (radius)
    double headDamage;
    double bodyDamage;
    double legDamage;
    double moveSpreadDeg; // extra inaccuracy at full running speed
    double speedMult;     // movement speed while holding it
};
SniperSpec GetSniperSpec(SniperWeapon w);

// ADAD strafing movement shared by Tracking and the sens finder test.
// Mimics Valorant: ~5.4 m/s run speed, very fast acceleration, random timing,
// occasional counter-strafe stops.
// What the last shot did, for HUD feedback (hit marker, points pop-up).
struct ShotFeedback {
    double time = -100.0;  // game time of the shot
    bool hit = false;
    bool head = false;
    long long points = 0;  // score change caused by the shot
};

struct Strafer {
    float x = 0.0f;
    float v = 0.0f;
    int dir = 1;
    double timer = 0.0;
    float minX = -4.5f;
    float maxX = 4.5f;
    float speedScale = 1.0f;  // difficulty

    void Reset(Rng& rng);
    void Update(double dt, Rng& rng);
};

class Mode {
public:
    Mode(ModeId id, const GameContext& ctx);
    virtual ~Mode() = default;
    Mode(const Mode&) = delete;
    Mode& operator=(const Mode&) = delete;

    ModeId Id() const { return id_; }

    // The countdown finished; the run starts at game time t.
    virtual void Begin(double t) = 0;
    // Once per frame while the run is live. triggerHeld = shoot bind held.
    virtual void Update(double t, double dt, bool triggerHeld) = 0;
    // The shoot bind was pressed. The camera is already rotated by all mouse
    // movement that happened before the click.
    virtual void OnShot(double t);
    // Called right after a frame was presented. Targets drawn for the first
    // time get their spawn time here, so reaction/TTK start when the player
    // could actually see the target.
    void OnPresented(double t);

    virtual void Draw3D() const;
    // Scope support (Sniper mode): current magnification (1 = not zoomed),
    // whether the normal crosshair is hidden, and mouse/key button events
    // other than shooting (bind code, pressed/released, game time).
    virtual double Zoom() const { return 1.0; }
    virtual bool HideCrosshair() const { return false; }
    // Full-screen overlay drawn under the HUD (e.g. the sniper scope).
    virtual void DrawOverlay() const {}
    virtual void OnButton(int /*bindCode*/, bool /*down*/, double /*t*/) {}
    // Modes that end on their own (VS Bot match) return true when done.
    virtual bool Finished() const { return false; }
    // Replace the run timer in the HUD (VS Bot shows the round timer and
    // score instead). Return false to use the normal timer.
    virtual bool HudTimer(double /*t*/, double& /*secondsLeft*/, std::string& /*label*/) const { return false; }
    // Mode specific HUD (virtual UI coordinates, see ui.h).
    virtual void DrawHud(double t) const;

    RunStats& Stats() { return stats_; }
    const RunStats& Stats() const { return stats_; }
    const ShotFeedback& LastShot() const { return lastShot_; }
    int Streak() const { return streak_; }
    int BestStreak() const { return bestStreak_; }

protected:
    // A shot hit targets_[index]. Default: score, sound, remove the target.
    virtual void OnHit(size_t index, const TargetHit& hit, double t);
    virtual void OnMiss(double t);
    virtual void OnTargetPresented(Target& target, double t);
    // Start time for time-to-kill of 'target'.
    virtual double TtkStart(const Target& target) const;
    // Aim at the head of humanoids (for shot analysis).
    virtual bool AimHead() const { return false; }
    // The ray a shot travels along (snipers add unscoped spread).
    virtual Ray ShotRay() { return ctx_.cam->AimRay(); }

    // Shared helpers
    void TickTargets(double dt);
    void UpdateReactionOnset(double t);
    void ArmReaction(double t);
    void AnalyzeShot(const Target& target, double t);
    int FindAimedTarget() const;
    void MarkFlickStart();
    Vector3 PointFromAngles(double yawDeg, double pitchDeg, double dist) const;
    void DrawOffscreenArrow(const Target& target) const;
    void UpdateTracking(const Target& target, double dt, bool triggerHeld, bool& onTarget);

    ModeId id_;
    GameContext ctx_;
    RunStats stats_;
    std::vector<Target> targets_;

    double lastKillTime_ = -1.0;
    double flickStartYaw_ = 0.0;
    double flickStartPitch_ = 0.0;

    bool useOnsetReaction_ = true;
    bool reactionArmed_ = false;
    double reactionArmTime_ = 0.0;

    double trackTickTimer_ = 0.0;

    ShotFeedback lastShot_;
    int streak_ = 0;      // hits in a row
    int bestStreak_ = 0;
};

std::unique_ptr<Mode> CreateMode(ModeId id, const GameContext& ctx);
std::unique_ptr<Mode> CreateVsBotMode(const GameContext& ctx);   // vsbot.cpp
std::unique_ptr<Mode> CreateSniperMode(const GameContext& ctx);  // sniper.cpp
