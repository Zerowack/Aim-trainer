// modes.cpp - Gridshot, Microshot, Tracking, Flick 180, Reaction, Peek and
// the mixed sens-finder test.
#include "modes.h"

#include <algorithm>
#include <cmath>

#include "ui.h"

namespace {

constexpr double kRunSpeed = 5.4;       // m/s, Valorant rifle run speed
constexpr double kStrafeAccel = 38.0;   // m/s^2, reaches full speed in ~0.14 s

double Clamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

}  // namespace

// ===========================================================================
// Strafer

void Strafer::Reset(Rng& rng) {
    x = 0.0f;
    v = 0.0f;
    dir = rng.Sign();
    timer = rng.Uniform(0.2, 0.6);
}

void Strafer::Update(double dt, Rng& rng) {
    timer -= dt;
    if (timer <= 0.0) {
        if (dir != 0 && rng.Chance(0.15)) {
            dir = 0;  // counter-strafe stop, like shooting in Valorant
            timer = rng.Uniform(0.12, 0.32);
        } else {
            if (dir == 0) dir = rng.Sign();
            else if (rng.Chance(0.8)) dir = -dir;  // mostly A-D-A-D, sometimes a long strafe
            timer = rng.Uniform(0.18, 0.75);
        }
    }
    // Keep inside the lane.
    if (x > maxX - 0.4f && dir > 0) { dir = -1; timer = rng.Uniform(0.25, 0.7); }
    if (x < minX + 0.4f && dir < 0) { dir = 1; timer = rng.Uniform(0.25, 0.7); }

    const double target = dir * kRunSpeed * speedScale;
    const double maxStep = kStrafeAccel * speedScale * dt;
    const double delta = Clamp(target - v, -maxStep, maxStep);
    v = static_cast<float>(v + delta);
    x = static_cast<float>(Clamp(x + v * dt, minX, maxX));
}

// ===========================================================================
// Mode base

Mode::Mode(ModeId id, const GameContext& ctx) : id_(id), ctx_(ctx) {
    stats_.mode = id;
    stats_.difficulty = ctx.difficulty;
    stats_.weapon = ctx.weapon;
}

SniperSpec GetSniperSpec(SniperWeapon w) {
    switch (w) {
        //                           name     mag  interval   reload zoom1 zoom2 hip  head   body   legs  move speed
        case SniperWeapon::Marshal: return {"Marshal", 5, 1.0 / 1.5, 2.5, 3.5, 0.0, 1.2, 202.0, 101.0, 85.0, 3.0, 1.0};
        case SniperWeapon::Outlaw: return {"Outlaw", 2, 1.0 / 2.75, 2.5, 3.5, 0.0, 2.2, 238.0, 140.0, 119.0, 4.0, 0.95};
        default: return {"Operator", 5, 1.0 / 0.6, 3.7, 2.5, 5.0, 5.0, 255.0, 150.0, 127.0, 6.0, 0.95};
    }
}

void Mode::OnPresented(double t) {
    for (Target& tg : targets_) {
        if (!tg.presented) {
            tg.presented = true;
            tg.spawnTime = t;
            OnTargetPresented(tg, t);
        }
    }
}

void Mode::OnTargetPresented(Target& /*target*/, double t) { ArmReaction(t); }

double Mode::TtkStart(const Target& target) const { return std::max(target.spawnTime, lastKillTime_); }

void Mode::OnShot(double t) {
    const Ray ray = ShotRay();
    const float coverDist = ctx_.world->RaycastCovers(ray);

    int best = -1;
    TargetHit bestHit;
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (!targets_[i].presented) continue;  // cannot shoot what was never on screen
        const TargetHit h = RaycastTarget(targets_[i], ray);
        if (h.hit && h.distance < coverDist && (best < 0 || h.distance < bestHit.distance)) {
            best = static_cast<int>(i);
            bestHit = h;
        }
    }

    stats_.shots++;
    const long long scoreBefore = stats_.score;
    const int aimed = best >= 0 ? best : FindAimedTarget();
    if (aimed >= 0) AnalyzeShot(targets_[static_cast<size_t>(aimed)], t);

    if (best >= 0) {
        const Target& tg = targets_[static_cast<size_t>(best)];
        stats_.hits++;
        if (bestHit.head) stats_.headshots++;
        const double ttk = (t - TtkStart(tg)) * 1000.0;
        if (ttk > 0.0 && ttk < 10000.0) {
            stats_.ttkSumMs += ttk;
            stats_.ttkCount++;
        }
        if (ctx_.fx) {
            const Vector3 p = {ray.position.x + ray.direction.x * bestHit.distance,
                               ray.position.y + ray.direction.y * bestHit.distance,
                               ray.position.z + ray.direction.z * bestHit.distance};
            const Color c = bestHit.head ? Color{255, 236, 190, 255} : ctx_.targetColor;
            ctx_.fx->Burst(p, c, bestHit.head ? 22 : 14, bestHit.head ? 5.0f : 3.8f, *ctx_.rng);
        }
        OnHit(static_cast<size_t>(best), bestHit, t);
        lastKillTime_ = t;
        ++streak_;
        if (streak_ > bestStreak_) bestStreak_ = streak_;
    } else {
        OnMiss(t);
        streak_ = 0;
    }
    lastShot_.time = t;
    lastShot_.hit = best >= 0;
    lastShot_.head = best >= 0 && bestHit.head;
    lastShot_.points = stats_.score - scoreBefore;
    MarkFlickStart();
}

void Mode::OnHit(size_t index, const TargetHit& hit, double /*t*/) {
    stats_.score += 100;
    ctx_.audio->Play(hit.head ? Sfx::Headshot : Sfx::Kill);
    targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
}

void Mode::OnMiss(double /*t*/) {
    // Half a kill: spamming clicks never beats clean shots.
    stats_.score -= 50;
    ctx_.audio->Play(Sfx::Miss);
}

void Mode::Draw3D() const {
    for (const Target& tg : targets_) DrawTarget(*ctx_.world, tg, ctx_.targetColor);
}

void Mode::DrawHud(double /*t*/) const {}

void Mode::TickTargets(double dt) {
    for (Target& tg : targets_) {
        if (tg.hitFlash > 0.0) tg.hitFlash -= dt;
    }
}

void Mode::ArmReaction(double t) {
    reactionArmed_ = true;
    reactionArmTime_ = t;
}

// Reaction time for aiming modes = time from a target appearing until the
// crosshair starts moving quickly (flick onset). Values under 80 ms mean the
// player was already moving (anticipation) and are ignored.
void Mode::UpdateReactionOnset(double t) {
    if (!useOnsetReaction_ || !reactionArmed_) return;
    const double elapsed = t - reactionArmTime_;
    if (elapsed > 1.5) {
        reactionArmed_ = false;
        return;
    }
    // 20 ms window: spans at least one frame at 50+ FPS, and is short enough
    // that a real flick (300+ deg/s) is detected right away.
    if (ctx_.history->Speed(t, 0.02) > 60.0) {
        const double ms = elapsed * 1000.0;
        if (ms >= 80.0) {
            stats_.reactionSumMs += ms;
            stats_.reactionCount++;
        }
        reactionArmed_ = false;
    }
}

// Measures where the crosshair was relative to the target at the moment of
// the click, split into the component along the direction the crosshair was
// moving. Positive along-motion error = target still ahead = undershoot;
// negative = crosshair already passed the target = overshoot.
void Mode::AnalyzeShot(const Target& target, double t) {
    const Vector3 eye = ctx_.cam->Eye();
    const Vector3 aim = TargetAimPoint(target, AimHead());
    double tYaw = 0.0, tPitch = 0.0;
    AnglesFromDirection(Vector3{aim.x - eye.x, aim.y - eye.y, aim.z - eye.z}, tYaw, tPitch);

    const double cosP = std::cos(val::DegToRad(ctx_.cam->Pitch()));
    const double ex = val::NormalizeDeg(tYaw - ctx_.cam->Yaw()) * cosP;
    const double ey = tPitch - ctx_.cam->Pitch();
    const double errDeg = std::sqrt(ex * ex + ey * ey);
    const double angR = std::max(0.05, TargetAngularRadius(target, eye, AimHead()));

    stats_.errDegSum += errDeg;
    stats_.errNormSum += errDeg / angR;
    stats_.errCount++;

    double mYaw = 0.0, mPitch = 0.0;
    if (!ctx_.history->DeltaOver(t, 0.08, mYaw, mPitch)) return;
    const double mx = mYaw * cosP, my = mPitch;
    const double mLen = std::sqrt(mx * mx + my * my);
    if (mLen < 0.35) return;  // crosshair was (almost) still: no direction to judge

    const double along = (ex * mx + ey * my) / mLen;
    if (std::fabs(along) < 0.25 * angR) {
        stats_.centered++;
    } else if (along > 0.0) {
        stats_.undershoots++;
    } else {
        stats_.overshoots++;
    }

    // Relative error vs. the size of the flick, used for sens suggestions.
    const double fx = val::NormalizeDeg(tYaw - flickStartYaw_) * cosP;
    const double fy = tPitch - flickStartPitch_;
    const double flickDist = std::sqrt(fx * fx + fy * fy);
    if (flickDist > 2.0 && flickDist > 3.0 * angR) {
        stats_.relErrorSum += Clamp(-along / flickDist, -0.5, 0.5);
        stats_.relErrorCount++;
    }
}

int Mode::FindAimedTarget() const {
    int best = -1;
    double bestAngle = 1e9;
    for (size_t i = 0; i < targets_.size(); ++i) {
        if (!targets_[i].presented) continue;
        const double a = AngleToPoint(*ctx_.cam, TargetAimPoint(targets_[i], AimHead()));
        if (a < bestAngle) {
            bestAngle = a;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Mode::MarkFlickStart() {
    flickStartYaw_ = ctx_.cam->Yaw();
    flickStartPitch_ = ctx_.cam->Pitch();
}

Vector3 Mode::PointFromAngles(double yawDeg, double pitchDeg, double dist) const {
    const Vector3 eye = ctx_.cam->Eye();
    const Vector3 d = DirectionFromAngles(yawDeg, pitchDeg);
    const float k = static_cast<float>(dist);
    return Vector3{eye.x + d.x * k, eye.y + d.y * k, eye.z + d.z * k};
}

void Mode::DrawOffscreenArrow(const Target& target) const {
    const Vector3 eye = ctx_.cam->Eye();
    const Vector3 p = TargetAimPoint(target, false);
    double tYaw = 0.0, tPitch = 0.0;
    AnglesFromDirection(Vector3{p.x - eye.x, p.y - eye.y, p.z - eye.z}, tYaw, tPitch);
    const double dx = val::NormalizeDeg(tYaw - ctx_.cam->Yaw());
    const double dy = tPitch - ctx_.cam->Pitch();
    const double aspect = static_cast<double>(GetScreenWidth()) / std::max(1, GetScreenHeight());
    const double halfH = val::GameHorizontalFov(aspect) * 0.5;
    const double halfV = val::GameVerticalFov() * 0.5;
    if (std::fabs(dx) < halfH * 0.92 && std::fabs(dy) < halfV * 0.92) return;  // visible

    const double len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6) return;
    const float ux = static_cast<float>(dx / len), uy = static_cast<float>(-dy / len);
    const float cx = ui::VW() * 0.5f, cy = ui::VH() * 0.5f;
    const float r = 150.0f;
    const Vector2 tip = {cx + ux * (r + 26.0f), cy + uy * (r + 26.0f)};
    const Vector2 base = {cx + ux * r, cy + uy * r};
    const Vector2 perp = {-uy * 14.0f, ux * 14.0f};
    ui::Tri(tip, Vector2{base.x - perp.x, base.y - perp.y}, Vector2{base.x + perp.x, base.y + perp.y},
            ui::Alpha(ui::theme::kAccent, 0.9f));
}

void Mode::UpdateTracking(const Target& target, double dt, bool triggerHeld, bool& onTarget) {
    onTarget = false;
    stats_.trackTotalTime += dt;
    if (!triggerHeld) return;
    stats_.trackHeldTime += dt;
    const TargetHit h = RaycastTarget(target, ctx_.cam->AimRay());
    if (!h.hit) return;
    onTarget = true;
    stats_.trackOnTime += dt;
    trackTickTimer_ -= dt;
    if (trackTickTimer_ <= 0.0) {
        ctx_.audio->Play(Sfx::Tick);
        trackTickTimer_ = 0.1;
    }
}

// ===========================================================================
// Gridshot: 3 body-sized targets on a 5x4 grid, 10 m away.

namespace {

class GridshotMode : public Mode {
public:
    explicit GridshotMode(const GameContext& ctx) : Mode(ModeId::Gridshot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        for (int i = 0; i < 3; ++i) Spawn(-1);
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double /*t*/) override {
        const int cell = targets_[index].tag;
        stats_.score += 100;
        ctx_.audio->Play(Sfx::Kill);
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        Spawn(cell);
    }

private:
    static constexpr int kCols = 5;
    static constexpr int kRows = 4;

    void Spawn(int avoidCell) {
        std::vector<int> free;
        for (int c = 0; c < kCols * kRows; ++c) {
            bool used = (c == avoidCell);
            for (const Target& tg : targets_) used = used || tg.tag == c;
            if (!used) free.push_back(c);
        }
        if (free.empty()) return;
        const int cell = free[static_cast<size_t>(ctx_.rng->Int(0, static_cast<int>(free.size()) - 1))];
        const int col = cell % kCols, row = cell / kCols;
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = 0.30f * ctx_.diff.size;
        tg.pos = Vector3{(static_cast<float>(col) - 2.0f) * 1.0f,
                         ValCamera::kEyeHeight + (static_cast<float>(row) - 1.5f) * 0.85f, -10.0f};
        tg.tag = cell;
        targets_.push_back(tg);
    }
};

// ===========================================================================
// Microshot: one head-sized target at a time, 10-16 m, short lifetime.

class MicroshotMode : public Mode {
public:
    explicit MicroshotMode(const GameContext& ctx) : Mode(ModeId::Microshot, ctx) {}

    void Begin(double /*t*/) override {
        targets_.clear();
        lastYaw_ = 0.0;
        lastPitch_ = 0.0;
        Spawn();
        MarkFlickStart();
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        for (size_t i = 0; i < targets_.size(); ++i) {
            const Target& tg = targets_[i];
            if (tg.presented && t - tg.spawnTime > tg.lifetime) {
                stats_.expired++;
                stats_.score -= 50;
                targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(i));
                Spawn();
                break;
            }
        }
    }

    void OnMiss(double /*t*/) override {
        // One bullet per target: a miss loses it.
        stats_.score -= 40;
        ctx_.audio->Play(Sfx::Miss);
        targets_.clear();
        Spawn();
    }

    void DrawHud(double t) const override {
        ui::Text("ONE BULLET PER TARGET", ui::VW() * 0.5f, ui::VH() * 0.5f + 72.0f, 16.0f,
                 ui::Alpha(ui::theme::kTextDim, 0.7f), ui::Align::Center);
        // Shrinking bar under the crosshair shows the remaining lifetime.
        if (targets_.empty() || !targets_[0].presented) return;
        const Target& tg = targets_[0];
        const float frac = static_cast<float>(Clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0));
        const float w = 120.0f;
        ui::Fill(Rectangle{ui::VW() * 0.5f - w * 0.5f, ui::VH() * 0.5f + 60.0f, w * frac, 4.0f},
                 ui::Alpha(ui::theme::kAccent, 0.8f));
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = Clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 100 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Headshot);
        targets_.erase(targets_.begin() + static_cast<std::ptrdiff_t>(index));
        Spawn();
    }

private:
    double lastYaw_ = 0.0;
    double lastPitch_ = 0.0;

    void Spawn() {
        // Next target 3-14 degrees away from the previous one, staying in a
        // cone in front of the player.
        double yaw = 0.0, pitch = 0.0;
        for (int tries = 0; tries < 40; ++tries) {
            yaw = ctx_.rng->Uniform(-14.0, 14.0);
            pitch = ctx_.rng->Uniform(-4.0, 7.0);
            const double d = std::hypot(yaw - lastYaw_, pitch - lastPitch_);
            if (d >= 3.0 && d <= 14.0) break;
        }
        lastYaw_ = yaw;
        lastPitch_ = pitch;
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = hitbox::kHeadRadius * ctx_.diff.size;
        tg.pos = PointFromAngles(yaw, pitch, ctx_.rng->Uniform(10.0, 16.0));
        tg.lifetime = 1.4 * ctx_.diff.time;
        targets_.push_back(tg);
    }
};

// ===========================================================================
// Tracking: humanoid doing ADAD strafes 13 m away. Hold fire on it.

class TrackingMode : public Mode {
public:
    explicit TrackingMode(const GameContext& ctx) : Mode(ModeId::Tracking, ctx) { useOnsetReaction_ = false; }

    void Begin(double /*t*/) override {
        targets_.clear();
        strafer_.Reset(*ctx_.rng);
        strafer_.speedScale = ctx_.diff.speed;
        Target tg;
        tg.kind = TargetKind::Humanoid;
        // Harder = further away (smaller on screen) and faster.
        tg.pos = Vector3{0.0f, 0.0f, -13.0f / ctx_.diff.size};
        targets_.push_back(tg);
    }

    void Update(double /*t*/, double dt, bool triggerHeld) override {
        TickTargets(dt);
        if (targets_.empty()) return;
        strafer_.Update(dt, *ctx_.rng);
        Target& tg = targets_[0];
        tg.pos.x = strafer_.x;
        bool on = false;
        UpdateTracking(tg, dt, triggerHeld, on);
        tg.beingHit = on;
        stats_.score = TrackingScore();
    }

    void OnShot(double /*t*/) override {}  // tracking is scored by time, not clicks

    void DrawHud(double /*t*/) const override {
        const double pct = stats_.trackTotalTime > 0.0 ? stats_.trackOnTime / stats_.trackTotalTime : 0.0;
        ui::Text(TextFormat("ON TARGET %.0f%%", pct * 100.0), ui::VW() * 0.5f, ui::VH() * 0.5f + 190.0f, 26.0f,
                 ui::Alpha(ui::theme::kText, 0.85f), ui::Align::Center);
    }

private:
    Strafer strafer_;
};

// ===========================================================================
// Flick 180: targets to the side or behind the player.

class Flick180Mode : public Mode {
public:
    explicit Flick180Mode(const GameContext& ctx) : Mode(ModeId::Flick180, ctx) {}

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
        if (!targets_.empty()) DrawOffscreenArrow(targets_[0]);
    }

protected:
    void OnHit(size_t index, const TargetHit& /*hit*/, double t) override {
        const Target& tg = targets_[index];
        const double remain = Clamp(1.0 - (t - tg.spawnTime) / tg.lifetime, 0.0, 1.0);
        stats_.score += 100 + static_cast<long long>(std::lround(remain * 100.0));
        ctx_.audio->Play(Sfx::Kill);
        targets_.clear();
        Spawn();
    }

private:
    void Spawn() {
        const double offset = ctx_.rng->Uniform(90.0, 180.0) * ctx_.rng->Sign();
        Target tg;
        tg.kind = TargetKind::Sphere;
        tg.radius = hitbox::kBodySphere * ctx_.diff.size;
        tg.pos = PointFromAngles(ctx_.cam->Yaw() + offset, ctx_.rng->Uniform(-4.0, 12.0), ctx_.rng->Uniform(10.0, 16.0));
        tg.lifetime = 3.5 * ctx_.diff.time;
        targets_.push_back(tg);
    }
};

// ===========================================================================
// Reaction: wait for a random delay, then click as fast as possible.

class ReactionMode : public Mode {
public:
    explicit ReactionMode(const GameContext& ctx) : Mode(ModeId::Reaction, ctx) { useOnsetReaction_ = false; }

    void Begin(double t) override {
        targets_.clear();
        Schedule(t);
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        switch (state_) {
            case State::Waiting:
                if (t >= showAt_) {
                    Target tg;
                    tg.kind = TargetKind::Sphere;
                    tg.radius = 0.55f * ctx_.diff.size;
                    // Easy/Normal: right under the crosshair (pure reaction).
                    // Hard/Insane: slightly off-centre, so a small flick is needed.
                    double off = 0.0;
                    if (ctx_.difficulty == Difficulty::Hard) off = 3.0;
                    if (ctx_.difficulty == Difficulty::Insane) off = 6.0;
                    const double ang = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
                    tg.pos = PointFromAngles(ctx_.cam->Yaw() + std::cos(ang) * off, ctx_.cam->Pitch() + std::sin(ang) * off, 8.0);
                    tg.lifetime = 1.5 * ctx_.diff.time;
                    targets_.push_back(tg);
                    state_ = State::Showing;
                }
                break;
            case State::Showing:
                if (!targets_.empty() && targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
                    stats_.expired++;
                    targets_.clear();
                    message_ = "TOO SLOW";
                    state_ = State::Feedback;
                    feedbackUntil_ = t + 0.8;
                }
                break;
            case State::Feedback:
                if (t >= feedbackUntil_) Schedule(t);
                break;
        }
    }

    void OnShot(double t) override {
        const bool visible = state_ == State::Showing && !targets_.empty() && targets_[0].presented;
        if (!visible) {
            if (state_ == State::Feedback) return;  // clicks between trials are ignored
            // Clicked before the target was on screen.
            stats_.shots++;
            stats_.score -= 100;
            ctx_.audio->Play(Sfx::Miss);
            targets_.clear();
            message_ = "TOO EARLY";
            state_ = State::Feedback;
            feedbackUntil_ = t + 0.8;
            return;
        }
        Mode::OnShot(t);
    }

    void DrawHud(double /*t*/) const override {
        const float cx = ui::VW() * 0.5f, cy = ui::VH() * 0.5f;
        if (state_ == State::Waiting) {
            ui::Text("WAIT...", cx, cy + 90.0f, 34.0f, ui::Alpha(ui::theme::kTextDim, 0.9f), ui::Align::Center);
        } else if (state_ == State::Feedback) {
            ui::Text(message_, cx, cy + 90.0f, 40.0f, ui::theme::kText, ui::Align::Center);
        }
        if (stats_.reactionCount > 0) {
            ui::Text(TextFormat("AVG %.0f ms  |  BEST %.0f ms", stats_.AvgReactionMs(), best_), cx, cy + 140.0f, 24.0f,
                     ui::Alpha(ui::theme::kTextDim, 0.9f), ui::Align::Center);
        }
    }

protected:
    void OnTargetPresented(Target& /*target*/, double /*t*/) override {}

    void OnHit(size_t /*index*/, const TargetHit& /*hit*/, double t) override {
        const double rt = (t - targets_[0].spawnTime) * 1000.0;
        stats_.reactionSumMs += rt;
        stats_.reactionCount++;
        if (best_ <= 0.0 || rt < best_) best_ = rt;
        stats_.score += std::max<long long>(0, static_cast<long long>(std::lround(1000.0 - rt)));
        ctx_.audio->Play(Sfx::Kill);
        targets_.clear();
        message_ = TextFormat("%.0f ms", rt);
        state_ = State::Feedback;
        feedbackUntil_ = t + 0.9;
    }

    void OnMiss(double /*t*/) override { ctx_.audio->Play(Sfx::Miss); }

    double TtkStart(const Target& target) const override { return target.spawnTime; }

private:
    enum class State { Waiting, Showing, Feedback };

    void Schedule(double t) {
        state_ = State::Waiting;
        showAt_ = t + ctx_.rng->Uniform(1.0, 3.5);
    }

    State state_ = State::Waiting;
    double showAt_ = 0.0;
    double feedbackUntil_ = 0.0;
    double best_ = 0.0;
    std::string message_;
};

// ===========================================================================
// Peek practice: agents swing out from behind three cover boxes for a short
// window, like holding an angle in Valorant.

class PeekMode : public Mode {
public:
    explicit PeekMode(const GameContext& ctx) : Mode(ModeId::Peek, ctx) {}
    ~PeekMode() override { ctx_.world->ClearCovers(); }
    PeekMode(const PeekMode&) = delete;
    PeekMode& operator=(const PeekMode&) = delete;

    void Begin(double t) override {
        std::vector<Box> covers;
        for (int i = -1; i <= 1; ++i) {
            covers.push_back(Box{Vector3{static_cast<float>(i) * 7.0f, 1.15f, -15.0f}, Vector3{2.6f, 2.3f, 0.8f}});
        }
        ctx_.world->SetCovers(covers);
        targets_.clear();
        phase_ = Phase::Idle;
        phaseEnd_ = t + ctx_.rng->Uniform(0.8, 1.6);
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        switch (phase_) {
            case Phase::Idle:
                if (t >= phaseEnd_) StartPeek(t);
                break;
            case Phase::Out:
                Move(exposedX_, dt);
                CheckVisible(t);
                if (targets_.empty()) break;
                if (std::fabs(targets_[0].pos.x - exposedX_) < 1e-3f) {
                    phase_ = Phase::Hold;
                    phaseEnd_ = t + ctx_.rng->Uniform(0.22, 0.55) * ctx_.diff.time;
                }
                break;
            case Phase::Hold:
                if (t >= phaseEnd_) phase_ = Phase::Back;
                break;
            case Phase::Back:
                Move(hiddenX_, dt);
                if (!targets_.empty() && std::fabs(targets_[0].pos.x - hiddenX_) < 1e-3f) {
                    // Got away.
                    targets_.clear();
                    stats_.expired++;
                    stats_.score -= 50;
                    EnterIdle(t);
                }
                break;
        }
    }

    void Draw3D() const override {
        ctx_.world->DrawCovers();
        Mode::Draw3D();
    }

protected:
    bool AimHead() const override { return true; }
    void OnTargetPresented(Target& /*target*/, double /*t*/) override {}  // reaction starts when visible
    double TtkStart(const Target& /*target*/) const override { return visibleTime_; }

    void OnHit(size_t /*index*/, const TargetHit& hit, double t) override {
        const double exposed = t - visibleTime_;
        stats_.score += 150 + (hit.head ? 100 : 0) + static_cast<long long>(std::lround(Clamp(1.0 - exposed, 0.0, 1.0) * 100.0));
        ctx_.audio->Play(hit.head ? Sfx::Headshot : Sfx::Kill);
        targets_.clear();
        EnterIdle(t);
    }

private:
    enum class Phase { Idle, Out, Hold, Back };

    void EnterIdle(double t) {
        phase_ = Phase::Idle;
        phaseEnd_ = t + ctx_.rng->Uniform(0.6, 1.8);
    }

    void StartPeek(double /*t*/) {
        const std::vector<Box>& covers = ctx_.world->Covers();
        if (covers.empty()) return;
        const Box& c = covers[static_cast<size_t>(ctx_.rng->Int(0, static_cast<int>(covers.size()) - 1))];
        side_ = ctx_.rng->Sign();

        // The agent stands just behind the cover. Because of perspective, the
        // cover edge "projects" further out at the agent's depth: a point at x
        // there lines up with x * (frontDist / pointDist) on the cover's front
        // face. The visible edge is computed at the agent's centre depth; the
        // hidden position uses the most conservative of the body's front and
        // back depth so no sliver of the agent shows while it waits.
        const Vector3 eye = ctx_.cam->Eye();
        const float coverEdge = c.center.x + static_cast<float>(side_) * c.size.x * 0.5f;
        const float frontDist = eye.z - (c.center.z + c.size.z * 0.5f);
        const float agentZ = c.center.z - c.size.z * 0.5f - 0.25f;
        const float agentDist = eye.z - agentZ;
        const float halfDepth = hitbox::kTorsoDepth * 0.5f + 0.03f;
        edgeX_ = coverEdge * agentDist / frontDist;
        const float edgeNear = coverEdge * (agentDist - halfDepth) / frontDist;
        const float edgeFar = coverEdge * (agentDist + halfDepth) / frontDist;
        const float hideEdge = side_ > 0 ? std::min(edgeNear, edgeFar) : std::max(edgeNear, edgeFar);

        const float halfBody = hitbox::kTorsoWidth * 0.5f;
        hiddenX_ = hideEdge - static_cast<float>(side_) * (halfBody + 0.06f);
        exposedX_ = edgeX_ + static_cast<float>(side_) * ctx_.rng->UniformF(0.35f, 0.85f);
        Target tg;
        tg.kind = TargetKind::Humanoid;
        tg.pos = Vector3{hiddenX_, 0.0f, agentZ};
        targets_.clear();
        targets_.push_back(tg);
        visible_ = false;
        phase_ = Phase::Out;
    }

    void Move(float toX, double dt) {
        if (targets_.empty()) return;
        float& x = targets_[0].pos.x;
        const float step = static_cast<float>(kRunSpeed * ctx_.diff.speed * dt);
        if (std::fabs(toX - x) <= step) x = toX;
        else x += (toX > x ? step : -step);
    }

    // The peek "starts" for reaction/TTK once any part clears the (projected) cover edge.
    void CheckVisible(double t) {
        if (visible_ || targets_.empty()) return;
        const float halfBody = hitbox::kTorsoWidth * 0.5f;
        if (static_cast<float>(side_) * (targets_[0].pos.x - edgeX_) > -halfBody) {
            visible_ = true;
            visibleTime_ = t;
            ArmReaction(t);
        }
    }

    Phase phase_ = Phase::Idle;
    double phaseEnd_ = 0.0;
    int side_ = 1;
    float edgeX_ = 0.0f;
    float hiddenX_ = 0.0f;
    float exposedX_ = 0.0f;
    bool visible_ = false;
    double visibleTime_ = 0.0;
};

// ===========================================================================
// Crosshair Placement: agents appear beside pillars placed around the range.
// You are scored on where your crosshair already was when an agent appeared
// (angle to its head), and on how much of the time your crosshair sits at
// head level - which in Valorant is the eye line (pitch ~0).

class PlacementMode : public Mode {
public:
    explicit PlacementMode(const GameContext& ctx) : Mode(ModeId::Placement, ctx) {}
    ~PlacementMode() override { ctx_.world->ClearCovers(); }
    PlacementMode(const PlacementMode&) = delete;
    PlacementMode& operator=(const PlacementMode&) = delete;

    void Begin(double t) override {
        // Pillars at different angles and distances form the "angles" to hold.
        const double yaws[kSpots] = {-58.0, -34.0, -14.0, 9.0, 30.0, 55.0};
        const double dists[kSpots] = {13.0, 20.0, 26.0, 16.0, 23.0, 14.0};
        std::vector<Box> covers;
        for (int i = 0; i < kSpots; ++i) {
            const Vector3 d = DirectionFromAngles(yaws[i], 0.0);
            spots_[i] = Vector3{d.x * static_cast<float>(dists[i]), 0.0f, d.z * static_cast<float>(dists[i])};
            spotYaw_[i] = yaws[i];
            covers.push_back(Box{Vector3{spots_[i].x, 1.5f, spots_[i].z}, Vector3{1.4f, 3.0f, 1.4f}});
        }
        ctx_.world->SetCovers(covers);
        targets_.clear();
        nextAt_ = t + ctx_.rng->Uniform(0.8, 1.6);
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        UpdateReactionOnset(t);
        // Head level = within 1.5 degrees of the eye line.
        stats_.headLevelTotal += dt;
        if (std::fabs(ctx_.cam->Pitch()) < 1.5) stats_.headLevelTime += dt;

        if (targets_.empty()) {
            if (t >= nextAt_) Spawn();
        } else if (targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
            stats_.expired++;
            stats_.score -= 50;
            targets_.clear();
            nextAt_ = t + ctx_.rng->Uniform(0.6, 1.5);
        }
    }

    void Draw3D() const override {
        ctx_.world->DrawCovers();
        Mode::Draw3D();
    }

    void DrawHud(double t) const override {
        const double hl = stats_.HeadLevelPct();
        ui::Text(TextFormat("HEAD LEVEL %.0f%%", std::max(0.0, hl) * 100.0), ui::VW() * 0.5f, ui::VH() * 0.5f + 190.0f, 22.0f,
                 ui::Alpha(std::fabs(ctx_.cam->Pitch()) < 1.5 ? ui::theme::kGood : ui::theme::kTextDim, 0.9f), ui::Align::Center);
        if (t - lastPlacementTime_ < 1.0) {
            const char* dir = lastPlacementVert_ < -0.8 ? "  (too low)" : (lastPlacementVert_ > 0.8 ? "  (too high)" : "");
            ui::Text(TextFormat("PLACEMENT %.1f deg%s", lastPlacementErr_, dir), ui::VW() * 0.5f, ui::VH() * 0.5f + 90.0f, 22.0f,
                     lastPlacementErr_ < 2.5 ? ui::theme::kGood : (lastPlacementErr_ < 6.0 ? ui::theme::kWarn : ui::theme::kAccent),
                     ui::Align::Center);
        }
    }

protected:
    bool AimHead() const override { return true; }
    double TtkStart(const Target& target) const override { return target.spawnTime; }

    void OnTargetPresented(Target& target, double t) override {
        ArmReaction(t);
        // Where was the crosshair when the agent became visible?
        const Vector3 eye = ctx_.cam->Eye();
        const Vector3 head = TargetAimPoint(target, true);
        double yaw = 0.0, pitch = 0.0;
        AnglesFromDirection(Vector3{head.x - eye.x, head.y - eye.y, head.z - eye.z}, yaw, pitch);
        const double err = AngleToPoint(*ctx_.cam, head);
        stats_.placementErrSum += err;
        stats_.placementVertSum += ctx_.cam->Pitch() - pitch;
        stats_.placementCount++;
        lastPlacementErr_ = err;
        lastPlacementVert_ = ctx_.cam->Pitch() - pitch;
        lastPlacementTime_ = t;
        placementAtSpawn_ = err;
    }

    void OnHit(size_t /*index*/, const TargetHit& hit, double t) override {
        const double exposed = t - targets_[0].spawnTime;
        // Good pre-aim is worth up to 150, a fast kill up to 100.
        const double placeBonus = std::max(0.0, 150.0 - placementAtSpawn_ * 25.0);
        stats_.score += 100 + (hit.head ? 50 : 0) + static_cast<long long>(std::lround(placeBonus)) +
                        static_cast<long long>(std::lround(Clamp(1.0 - exposed, 0.0, 1.0) * 100.0));
        ctx_.audio->Play(hit.head ? Sfx::Headshot : Sfx::Kill);
        targets_.clear();
        nextAt_ = t + ctx_.rng->Uniform(0.6, 1.5);
    }

private:
    static constexpr int kSpots = 6;

    void Spawn() {
        const int i = ctx_.rng->Int(0, kSpots - 1);
        const int side = ctx_.rng->Sign();
        // "Right" relative to the view direction towards the pillar.
        const double y = val::DegToRad(spotYaw_[i]);
        const Vector3 right = {static_cast<float>(std::cos(y)), 0.0f, static_cast<float>(std::sin(y))};
        const float off = static_cast<float>(side) * 1.05f;
        Target tg;
        tg.kind = TargetKind::Humanoid;
        tg.pos = Vector3{spots_[i].x + right.x * off, 0.0f, spots_[i].z + right.z * off};
        tg.lifetime = 1.3 * ctx_.diff.time;
        targets_.push_back(tg);
    }

    Vector3 spots_[kSpots] = {};
    double spotYaw_[kSpots] = {};
    double nextAt_ = 0.0;
    double placementAtSpawn_ = 0.0;
    double lastPlacementErr_ = 0.0;
    double lastPlacementVert_ = 0.0;
    double lastPlacementTime_ = -10.0;
};


// ===========================================================================
// Mixed test for the sensitivity finder (20 s):
//   0-7 s flicks, 7-14 s tracking, 14-20 s micro-adjustments.

class MixedMode : public Mode {
public:
    explicit MixedMode(const GameContext& ctx) : Mode(ModeId::Mixed, ctx) {}

    void Begin(double t) override {
        beginTime_ = t;
        segment_ = -1;
        targets_.clear();
    }

    void Update(double t, double dt, bool triggerHeld) override {
        TickTargets(dt);
        const double e = t - beginTime_;
        const int seg = e < 7.0 ? 0 : (e < 14.0 ? 1 : 2);
        if (seg != segment_) EnterSegment(seg);

        if (segment_ == 1) {
            if (targets_.empty()) return;
            strafer_.Update(dt, *ctx_.rng);
            Target& tg = targets_[0];
            tg.pos.x = strafer_.x;
            bool on = false;
            UpdateTracking(tg, dt, triggerHeld, on);
            tg.beingHit = on;
        } else {
            UpdateReactionOnset(t);
            if (!targets_.empty() && targets_[0].presented && t - targets_[0].spawnTime > targets_[0].lifetime) {
                stats_.expired++;
                targets_.clear();
                SpawnForSegment();
            }
        }
        stats_.score = stats_.hits * 100 + TrackingScore();
    }

    void OnShot(double t) override {
        if (segment_ == 1) return;  // tracking segment: scored by time
        Mode::OnShot(t);
    }

    void DrawHud(double /*t*/) const override {
        static const char* const names[] = {"FLICKS", "TRACKING - HOLD FIRE", "MICRO-ADJUSTMENTS"};
        if (segment_ >= 0) {
            ui::Text(names[segment_], ui::VW() * 0.5f, 120.0f, 26.0f, ui::Alpha(ui::theme::kAccent, 0.9f), ui::Align::Center);
        }
        if (segment_ == 0 && !targets_.empty()) DrawOffscreenArrow(targets_[0]);
    }

protected:
    void OnHit(size_t /*index*/, const TargetHit& /*hit*/, double /*t*/) override {
        ctx_.audio->Play(segment_ == 2 ? Sfx::Headshot : Sfx::Kill);
        targets_.clear();
        SpawnForSegment();
    }

    void OnMiss(double /*t*/) override { ctx_.audio->Play(Sfx::Miss); }

private:
    void EnterSegment(int seg) {
        segment_ = seg;
        targets_.clear();
        reactionArmed_ = false;
        if (seg == 1) strafer_.Reset(*ctx_.rng);
        SpawnForSegment();
        MarkFlickStart();
    }

    void SpawnForSegment() {
        Target tg;
        if (segment_ == 0) {
            const double off = ctx_.rng->Uniform(20.0, 75.0) * ctx_.rng->Sign();
            tg.kind = TargetKind::Sphere;
            tg.radius = hitbox::kBodySphere;
            tg.pos = PointFromAngles(ctx_.cam->Yaw() + off, ctx_.rng->Uniform(-5.0, 10.0), ctx_.rng->Uniform(10.0, 15.0));
            tg.lifetime = 3.0;
        } else if (segment_ == 1) {
            tg.kind = TargetKind::Humanoid;
            tg.pos = Vector3{strafer_.x, 0.0f, -12.0f};
        } else {
            const double ang = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
            const double dist = ctx_.rng->Uniform(2.0, 8.0);
            const double pitch = Clamp(ctx_.cam->Pitch() + std::sin(ang) * dist, -8.0, 12.0);
            tg.kind = TargetKind::Sphere;
            tg.radius = hitbox::kHeadRadius;
            tg.pos = PointFromAngles(ctx_.cam->Yaw() + std::cos(ang) * dist, pitch, ctx_.rng->Uniform(10.0, 14.0));
            tg.lifetime = 1.6;
        }
        targets_.push_back(tg);
    }

    double beginTime_ = 0.0;
    int segment_ = -1;
    Strafer strafer_;
};

}  // namespace

std::unique_ptr<Mode> CreateMode(ModeId id, const GameContext& ctx) {
    switch (id) {
        case ModeId::Gridshot: return std::make_unique<GridshotMode>(ctx);
        case ModeId::Microshot: return std::make_unique<MicroshotMode>(ctx);
        case ModeId::Tracking: return std::make_unique<TrackingMode>(ctx);
        case ModeId::Flick180: return std::make_unique<Flick180Mode>(ctx);
        case ModeId::Reaction: return std::make_unique<ReactionMode>(ctx);
        case ModeId::Peek: return std::make_unique<PeekMode>(ctx);
        case ModeId::Placement: return std::make_unique<PlacementMode>(ctx);
        case ModeId::Sniper: return CreateSniperMode(ctx);
        case ModeId::VsBot: return CreateVsBotMode(ctx);
        case ModeId::Mixed: return std::make_unique<MixedMode>(ctx);
        default: return std::make_unique<GridshotMode>(ctx);
    }
}
