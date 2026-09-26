// modes.h - The training modes.
//
// Every mode works on the "game clock": seconds from platform::Now() with all
// paused time removed, so pausing never affects reaction time or TTK.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio.h"
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
    Color targetColor = Color{80, 220, 255, 255};
    float brightness = 1.0f;  // map brightness (targets are never dimmed)
};

// ADAD strafing movement shared by Tracking and the sens finder test.
// Mimics Valorant: ~5.4 m/s run speed, very fast acceleration, random timing,
// occasional counter-strafe stops.
struct Strafer {
    float x = 0.0f;
    float v = 0.0f;
    int dir = 1;
    double timer = 0.0;
    float minX = -4.5f;
    float maxX = 4.5f;

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
    // Mode specific HUD (virtual UI coordinates, see ui.h).
    virtual void DrawHud(double t) const;

    RunStats& Stats() { return stats_; }
    const RunStats& Stats() const { return stats_; }

protected:
    // A shot hit targets_[index]. Default: score, sound, remove the target.
    virtual void OnHit(size_t index, const TargetHit& hit, double t);
    virtual void OnMiss(double t);
    virtual void OnTargetPresented(Target& target, double t);
    // Start time for time-to-kill of 'target'.
    virtual double TtkStart(const Target& target) const;
    // Aim at the head of humanoids (for shot analysis).
    virtual bool AimHead() const { return false; }

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
};

std::unique_ptr<Mode> CreateMode(ModeId id, const GameContext& ctx);
