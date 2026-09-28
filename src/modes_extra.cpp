// modes_extra.cpp - classic aim-trainer scenarios:
//   Headshot, Sixshot, Spidershot, Motionshot, Smooth Tracking, Strafe Tap,
//   Target Switch, Long Range, Microflex and Popcorn.
//
// All of them use the shared Mode machinery (raw-input shots judged at the
// exact crosshair position, reaction / TTK / over-undershoot analysis) and
// the difficulty parameters (target size, time window, speed).
#include <algorithm>
#include <cmath>
#include <vector>

#include "modes.h"
#include "ui.h"

namespace {

// Turns a humanoid so it faces the player (standing at the origin).
void FaceOrigin(Target& t) {
    t.yaw = static_cast<float>(val::RadToDeg(std::atan2(-t.pos.x, t.pos.z)));
}

float Dist3(Vector3 a, Vector3 b) {
    const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ===========================================================================
// Headshot: agents appear around you one at a time; only headshots count.

class HeadshotMode : public Mode {
public:
    explicit HeadshotMode(const GameContext& ctx) : Mode(ModeId::Headshot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        if (!targets_.empty() && targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
            stats_.expired++;
            stats_.score -= 50;
            targets_.clear();
            Spawn();
        }
    }

    void DrawHud(double /*t*/) const override {
        ui::Text("HEADSHOTS ONLY", ui::VW() * 0.5f, ui::VH() * 0.5f + 72.0f, 16.0f, ui::Alpha(ui::theme::kTextDim, 0.7f),
                 ui::Align::Center);
    }

protected:
    bool CountsAsHit(const TargetHit& h) const override { return h.head; }
    bool AimHead() const override { return true; }

    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = std::clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 100 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Headshot);
        targets_.clear();
        Spawn();
    }

private:
    void Spawn() {
        double yaw = 0.0;
        for (int i = 0; i < 20; ++i) {
            yaw = ctx_.rng->Uniform(-38.0, 38.0);
            if (std::fabs(yaw - lastYaw_) > 8.0) break;
        }
        lastYaw_ = yaw;
        const double dist = ctx_.rng->Uniform(10.0, 20.0) / static_cast<double>(ctx_.diff.size);
        Target tg;
        tg.kind = TargetKind::Humanoid;
        const Vector3 p = PointFromAngles(yaw, 0.0, dist);
        tg.pos = Vector3{p.x, 0.0f, p.z};
        FaceOrigin(tg);
        tg.lifetime = 1.8 * ctx_.diff.time;
        targets_.push_back(tg);
    }
    double lastYaw_ = 0.0;
};

// ===========================================================================
// Sixshot: six small targets on a wall; one destroyed = a new one elsewhere.

class SixshotMode : public Mode {
public:
    explicit SixshotMode(const GameContext& ctx) : Mode(ModeId::Sixshot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        for (int i = 0; i < 6; ++i) Spawn(Vector3{0.0f, -100.0f, 0.0f});
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double /*t*/) override {
        const Vector3 where = targets_[index].pos;
        stats_.score += 100;
        ctx_.audio->Play(Sfx::Kill);
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        Spawn(where);
    }

private:
    void Spawn(Vector3 avoid) {
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.2f * ctx_.diff.size;
        for (int tries = 0; tries < 60; ++tries) {
            tg.pos = Vector3{ctx_.rng->UniformF(-3.6f, 3.6f), ctx_.rng->UniformF(0.5f, 3.1f), -12.0f};
            bool ok = Dist3(tg.pos, avoid) > 1.0f;
            for (const Target& o : targets_) ok = ok && Dist3(tg.pos, o.pos) > 0.8f;
            if (ok) break;
        }
        targets_.push_back(tg);
    }
};

// ===========================================================================
// Spidershot: centre target, then one out on the wall, then the centre again.

class SpidershotMode : public Mode {
public:
    explicit SpidershotMode(const GameContext& ctx) : Mode(ModeId::Spidershot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        centreNext_ = true;
        Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
    }

protected:
    void OnHit(size_t /*index*/, const TargetHit& /*hit*/, double /*t*/) override {
        stats_.score += 100;
        ctx_.audio->Play(Sfx::Kill);
        targets_.clear();
        Spawn();
    }

private:
    void Spawn() {
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.26f * ctx_.diff.size;
        if (centreNext_) {
            tg.pos = PointFromAngles(0.0, 0.0, 10.0);
        } else {
            double yaw = 0.0, pitch = 0.0;
            do {
                yaw = ctx_.rng->Uniform(-30.0, 30.0) * static_cast<double>(ctx_.diff.speed);
                pitch = ctx_.rng->Uniform(-9.0, 14.0);
            } while (std::hypot(yaw, pitch) < 8.0);
            tg.pos = PointFromAngles(yaw, pitch, 10.0);
        }
        centreNext_ = !centreNext_;
        targets_.push_back(tg);
    }
    bool centreNext_ = true;
};

// ===========================================================================
// Motionshot: three targets drifting across the wall.

class MotionshotMode : public Mode {
public:
    explicit MotionshotMode(const GameContext& ctx) : Mode(ModeId::Motionshot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        vel_.clear();
        turn_.clear();
        for (int i = 0; i < 3; ++i) Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        const float fdt = static_cast<float>(dt);
        for (size_t i = 0; i < targets_.size(); ++i) {
            Target& tg = targets_[i];
            tg.pos.x += vel_[i].x * fdt;
            tg.pos.y += vel_[i].y * fdt;
            if (tg.pos.x < -4.2f || tg.pos.x > 4.2f) vel_[i].x = -vel_[i].x;
            if (tg.pos.y < 0.5f || tg.pos.y > 3.3f) vel_[i].y = -vel_[i].y;
            tg.pos.x = std::clamp(tg.pos.x, -4.2f, 4.2f);
            tg.pos.y = std::clamp(tg.pos.y, 0.5f, 3.3f);
            turn_[i] -= dt;
            if (turn_[i] <= 0.0) NewVelocity(i);
        }
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double /*t*/) override {
        stats_.score += 100;
        ctx_.audio->Play(Sfx::Kill);
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        vel_.erase(vel_.begin() + static_cast<std::ptrdiff_t>(index));
        turn_.erase(turn_.begin() + static_cast<std::ptrdiff_t>(index));
        Spawn();
    }

private:
    void Spawn() {
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.3f * ctx_.diff.size;
        tg.pos = Vector3{ctx_.rng->UniformF(-4.0f, 4.0f), ctx_.rng->UniformF(0.7f, 3.1f), -11.0f};
        targets_.push_back(tg);
        vel_.push_back(Vector2{0.0f, 0.0f});
        turn_.push_back(0.0);
        NewVelocity(targets_.size() - 1);
    }
    void NewVelocity(size_t i) {
        const float a = ctx_.rng->UniformF(0.0f, 6.2831853f);
        const float sp = ctx_.rng->UniformF(1.5f, 3.5f) * ctx_.diff.speed;
        vel_[i] = Vector2{std::cos(a) * sp, std::sin(a) * sp * 0.6f};
        turn_[i] = ctx_.rng->Uniform(0.6, 1.5);
    }
    std::vector<Vector2> vel_;
    std::vector<double> turn_;
};

// ===========================================================================
// Smooth Tracking: a sphere on smooth curves; hold fire on it.

class SmoothTrackMode : public Mode {
public:
    explicit SmoothTrackMode(const GameContext& ctx) : Mode(ModeId::SmoothTrack, ctx) {}

    void Begin(double t) override {
        targets_.clear();
        start_ = t;
        for (double& p : phase_) p = ctx_.rng->Uniform(0.0, 6.2831853);
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.26f * ctx_.diff.size;
        tg.pos = Vector3{0.0f, 1.6f, -12.0f};
        targets_.push_back(tg);
    }

    void Update(double t, double dt, bool triggerHeld) override {
        TickTargets(dt);
        if (targets_.empty()) return;
        // Sum of sines at unrelated frequencies = smooth, never repeating path.
        const double k = 1.15 * static_cast<double>(ctx_.diff.speed);
        const double s = (t - start_) * k;
        Target& tg = targets_[0];
        tg.pos.x = static_cast<float>(3.4 * std::sin(0.83 * s + phase_[0]) + 1.3 * std::sin(2.17 * s + phase_[1]));
        tg.pos.y = static_cast<float>(1.7 + 0.8 * std::sin(1.31 * s + phase_[2]) + 0.35 * std::sin(2.9 * s + phase_[3]));
        bool on = false;
        UpdateTracking(tg, dt, triggerHeld, on);
        tg.beingHit = on;
        stats_.score = TrackingScore();
    }

    void OnShot(double /*t*/) override {}  // scored by time on target

    void DrawHud(double /*t*/) const override {
        const double pct = stats_.trackTotalTime > 0.0 ? stats_.trackOnTime / stats_.trackTotalTime : 0.0;
        ui::Text(TextFormat("ON TARGET %.0f%%", pct * 100.0), ui::VW() * 0.5f, ui::VH() * 0.5f + 190.0f, 26.0f,
                 ui::Alpha(ui::theme::kText, 0.85f), ui::Align::Center);
    }

private:
    double start_ = 0.0;
    double phase_[4] = {0.0, 0.0, 0.0, 0.0};
};

// ===========================================================================
// Strafe Tap: one-tap the head of an agent doing ADAD strafes.

class StrafeTapMode : public Mode {
public:
    explicit StrafeTapMode(const GameContext& ctx) : Mode(ModeId::StrafeTap, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        if (targets_.empty()) return;
        strafer_.Update(dt, *ctx_.rng);
        Target& tg = targets_[0];
        tg.pos.x = baseX_ + strafer_.x;
        tg.moveSpeed = std::fabs(strafer_.v);
        tg.walkPhase += static_cast<float>(std::fabs(strafer_.v) * dt * 2.6);
        if (tg.presented && t - tg.spawnTime > tg.lifetime) {
            stats_.expired++;
            stats_.score -= 50;
            targets_.clear();
            Spawn();
        }
    }

    void DrawHud(double /*t*/) const override {
        ui::Text("ONE-TAP THE HEAD", ui::VW() * 0.5f, ui::VH() * 0.5f + 72.0f, 16.0f, ui::Alpha(ui::theme::kTextDim, 0.7f),
                 ui::Align::Center);
    }

protected:
    bool CountsAsHit(const TargetHit& h) const override { return h.head; }
    bool AimHead() const override { return true; }

    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = std::clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 150 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Headshot);
        targets_.clear();
        Spawn();
    }

private:
    void Spawn() {
        strafer_.Reset(*ctx_.rng);
        strafer_.speedScale = ctx_.diff.speed;
        strafer_.minX = -3.0f;
        strafer_.maxX = 3.0f;
        baseX_ = ctx_.rng->UniformF(-4.0f, 4.0f);
        Target tg;
        tg.kind = TargetKind::Humanoid;
        tg.pos = Vector3{baseX_, 0.0f, -ctx_.rng->UniformF(11.0f, 16.0f) / ctx_.diff.size};
        FaceOrigin(tg);
        tg.lifetime = 2.6 * ctx_.diff.time;
        targets_.push_back(tg);
    }
    Strafer strafer_;
    float baseX_ = 0.0f;
};

// ===========================================================================
// Target Switch: three strafing agents with health. Track one until it
// dies, then switch.

class TargetSwitchMode : public Mode {
public:
    explicit TargetSwitchMode(const GameContext& ctx) : Mode(ModeId::TargetSwitch, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        for (int i = 0; i < 3; ++i) {
            lanes_[i].hp = 1.0;
            Respawn(static_cast<size_t>(i), true);
        }
    }

    void Update(double t, double dt, bool triggerHeld) override {
        TickTargets(dt);
        for (size_t i = 0; i < targets_.size(); ++i) {
            Lane& l = lanes_[i];
            l.strafer.Update(dt, *ctx_.rng);
            Target& tg = targets_[i];
            tg.pos.x = l.strafer.x;
            tg.moveSpeed = std::fabs(l.strafer.v);
            tg.walkPhase += static_cast<float>(std::fabs(l.strafer.v) * dt * 2.6);
            tg.beingHit = false;
        }
        stats_.trackTotalTime += dt;
        if (triggerHeld) {
            stats_.trackHeldTime += dt;
            // Nearest agent under the crosshair takes damage while you hold fire.
            const Ray ray = ctx_.cam->AimRay();
            int best = -1;
            float bestD = 1e30f;
            for (size_t i = 0; i < targets_.size(); ++i) {
                const TargetHit h = RaycastTarget(targets_[i], ray);
                if (h.hit && h.distance < bestD) {
                    bestD = h.distance;
                    best = static_cast<int>(i);
                }
            }
            if (best >= 0) {
                const size_t i = static_cast<size_t>(best);
                stats_.trackOnTime += dt;
                targets_[i].beingHit = true;
                lanes_[i].hp -= dt / kKillTime;
                tick_ -= dt;
                if (tick_ <= 0.0) {
                    ctx_.audio->Play(Sfx::Tick);
                    tick_ = 0.1;
                }
                if (lanes_[i].hp <= 0.0) {
                    stats_.hits++;
                    stats_.ttkSumMs += (t - lanes_[i].since) * 1000.0;
                    stats_.ttkCount++;
                    ctx_.audio->Play(Sfx::Kill);
                    if (ctx_.fx) ctx_.fx->Burst(TargetAimPoint(targets_[i], false), ctx_.targetColor, 16, 4.0f, *ctx_.rng);
                    lastShot_.time = t;
                    lastShot_.hit = true;
                    lastShot_.points = 100;
                    Respawn(i, false);
                    lanes_[i].since = t;
                }
            }
        }
        stats_.score = stats_.hits * 100 + TrackingScore();
    }

    void OnShot(double /*t*/) override {}  // damage comes from holding fire on target

    void Draw3D() const override {
        Mode::Draw3D();
        // Health bars above the heads.
        for (size_t i = 0; i < targets_.size(); ++i) {
            const Vector3 head = TargetAimPoint(targets_[i], true);
            const float hp = static_cast<float>(std::max(0.0, lanes_[i].hp));
            ctx_.world->DrawBoxLit(Vector3{head.x, head.y + 0.32f, head.z}, Vector3{0.62f, 0.07f, 0.02f}, Color{30, 30, 36, 255},
                                   Surface::Plain);
            ctx_.world->DrawBoxLit(Vector3{head.x - 0.3f * (1.0f - hp), head.y + 0.32f, head.z + 0.012f}, Vector3{0.6f * hp, 0.05f, 0.02f},
                                   Color{90, 230, 140, 255}, Surface::Glow);
        }
    }

private:
    struct Lane {
        Strafer strafer;
        double hp = 1.0;
        double since = 0.0;
    };
    static constexpr double kKillTime = 0.45;  // seconds on target to kill

    void Respawn(size_t i, bool first) {
        Lane& l = lanes_[i];
        l.hp = 1.0;
        const float centre = -5.0f + static_cast<float>(i) * 5.0f + ctx_.rng->UniformF(-1.0f, 1.0f);
        l.strafer.Reset(*ctx_.rng);
        l.strafer.speedScale = ctx_.diff.speed;
        l.strafer.minX = centre - 2.2f;
        l.strafer.maxX = centre + 2.2f;
        l.strafer.x = centre;
        Target tg;
        tg.kind = TargetKind::Humanoid;
        tg.pos = Vector3{centre, 0.0f, -ctx_.rng->UniformF(10.0f, 15.0f) / ctx_.diff.size};
        FaceOrigin(tg);
        tg.presented = true;
        if (first) targets_.push_back(tg);
        else targets_[i] = tg;
    }
    Lane lanes_[3];
    double tick_ = 0.0;
};

// ===========================================================================
// Long Range: tiny targets far away.

class LongRangeMode : public Mode {
public:
    explicit LongRangeMode(const GameContext& ctx) : Mode(ModeId::LongRange, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        if (!targets_.empty() && targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
            stats_.expired++;
            stats_.score -= 50;
            targets_.clear();
            Spawn();
        }
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = std::clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 100 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Kill);
        targets_.clear();
        Spawn();
    }

private:
    void Spawn() {
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.16f * ctx_.diff.size;
        tg.pos = PointFromAngles(ctx_.rng->Uniform(-16.0, 16.0), ctx_.rng->Uniform(-1.5, 5.0), ctx_.rng->Uniform(32.0, 45.0));
        tg.lifetime = 3.0 * ctx_.diff.time;
        targets_.push_back(tg);
    }
};

// ===========================================================================
// Microflex: tiny targets right next to the crosshair.

class MicroflexMode : public Mode {
public:
    explicit MicroflexMode(const GameContext& ctx) : Mode(ModeId::Microflex, ctx) {}

    void Begin(double t) override {
        targets_.clear();
        nextAt_ = t + 0.3;
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        if (targets_.empty()) {
            if (t >= nextAt_) Spawn();
        } else if (targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
            stats_.expired++;
            stats_.score -= 50;
            targets_.clear();
            nextAt_ = t + ctx_.rng->Uniform(0.15, 0.4);
        }
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = std::clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 100 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Kill);
        targets_.clear();
        nextAt_ = t + ctx_.rng->Uniform(0.15, 0.4);
    }

private:
    void Spawn() {
        // 1.5-5 degrees from where you are aiming now, kept inside a comfortable cone.
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double off = ctx_.rng->Uniform(1.5, 5.0);
        double yaw = ctx_.cam->Yaw() + std::cos(a) * off;
        double pitch = ctx_.cam->Pitch() + std::sin(a) * off;
        yaw = std::clamp(val::NormalizeDeg(yaw), -35.0, 35.0);
        pitch = std::clamp(pitch, -10.0, 15.0);
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.12f * ctx_.diff.size;
        tg.pos = PointFromAngles(yaw, pitch, 8.0);
        tg.lifetime = 0.9 * ctx_.diff.time;
        targets_.push_back(tg);
        MarkFlickStart();
    }
    double nextAt_ = 0.0;
};

// ===========================================================================
// Popcorn: targets thrown into the air; hit them before they land.

class PopcornMode : public Mode {
public:
    explicit PopcornMode(const GameContext& ctx) : Mode(ModeId::Popcorn, ctx) {}

    void Begin(double t) override {
        targets_.clear();
        vel_.clear();
        nextAt_ = t;
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        const float k = ctx_.diff.speed;
        const float g = 9.8f * k * k;
        const float fdt = static_cast<float>(dt);
        for (size_t i = 0; i < targets_.size();) {
            Target& tg = targets_[i];
            vel_[i].y -= g * fdt;
            tg.pos.x += vel_[i].x * fdt;
            tg.pos.y += vel_[i].y * fdt;
            if (tg.pos.y < -0.3f) {
                stats_.expired++;
                stats_.score -= 50;
                targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(i));
                vel_.erase(vel_.begin() + static_cast<std::ptrdiff_t>(i));
                continue;
            }
            ++i;
        }
        // Up to three in the air; a fast player is never left waiting for targets.
        if (targets_.size() < 3 && t >= nextAt_) {
            Launch();
            nextAt_ = t + ctx_.rng->Uniform(0.18, 0.32) / static_cast<double>(k);
        }
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double /*t*/) override {
        stats_.score += 100;
        ctx_.audio->Play(Sfx::Kill);
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        vel_.erase(vel_.begin() + static_cast<std::ptrdiff_t>(index));
    }

private:
    void Launch() {
        const float k = ctx_.diff.speed;
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.3f * ctx_.diff.size;
        tg.pos = Vector3{ctx_.rng->UniformF(-4.5f, 4.5f), 0.2f, -ctx_.rng->UniformF(10.0f, 13.0f)};
        targets_.push_back(tg);
        vel_.push_back(Vector2{ctx_.rng->UniformF(-1.5f, 1.5f) * k, ctx_.rng->UniformF(7.0f, 9.0f) * k});
    }
    std::vector<Vector2> vel_;
    double nextAt_ = 0.0;
};

}  // namespace

std::unique_ptr<Mode> CreateExtraMode(ModeId id, const GameContext& ctx) {
    switch (id) {
        case ModeId::Headshot: return std::make_unique<HeadshotMode>(ctx);
        case ModeId::Sixshot: return std::make_unique<SixshotMode>(ctx);
        case ModeId::Spidershot: return std::make_unique<SpidershotMode>(ctx);
        case ModeId::Motionshot: return std::make_unique<MotionshotMode>(ctx);
        case ModeId::SmoothTrack: return std::make_unique<SmoothTrackMode>(ctx);
        case ModeId::StrafeTap: return std::make_unique<StrafeTapMode>(ctx);
        case ModeId::TargetSwitch: return std::make_unique<TargetSwitchMode>(ctx);
        case ModeId::LongRange: return std::make_unique<LongRangeMode>(ctx);
        case ModeId::Microflex: return std::make_unique<MicroflexMode>(ctx);
        default: return std::make_unique<PopcornMode>(ctx);
    }
}
