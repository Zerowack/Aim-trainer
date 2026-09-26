// sniper.cpp - Sniper mode: hold (and retake) a long angle with a Marshal,
// Outlaw or Operator, like holding C long on Haven.
//
// A long lane ends in a big wall with two side boxes in front of it.
// Enemies peek from behind them, one at a time, from random sides. You stand
// on the other end with your own cover and move like in Valorant: shots
// fired while moving (or in the air) whiff, so you peek, stop, then shoot.
//
// Difficulty changes what the enemies do:
//   Easy    static peeks from the left or right of the big wall, no return fire
//   Normal  swings and crouch peeks from all four spots, enemies shoot back
//   Hard    wide swings, jump peeks, jiggle baits, and enemies holding angles
//           that you have to peek (retake) yourself
//   Insane  all of it with shorter peeks and faster, more accurate enemies
//
// Weapon and movement numbers are approximations of Valorant.
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "modes.h"
#include "movement.h"
#include "ui.h"

namespace {

using namespace mv;

// ---- Tuning ------------------------------------------------------------------
constexpr double kEnemyHp = 150.0;       // 100 health + 50 heavy shields
constexpr double kPlayerHp = 150.0;
constexpr double kRespawnDelay = 1.5;
constexpr float kScopedSpeedMult = 0.76f;  // scoped you move at about 3/4 speed
constexpr double kAirSpread = 10.0;        // degrees, jumping / falling
constexpr float kPlayerMaxZ = -3.0f;       // you can't cross the middle of the lane

// Enemy rifle (Vandal-like taps).
constexpr double kRifleHead = 160.0;
constexpr double kRifleBody = 40.0;
constexpr double kRifleLegs = 34.0;
// Enemy Operator (Hard / Insane).
constexpr double kOpHead = 255.0;
constexpr double kOpBody = 150.0;
constexpr double kOpLegs = 127.0;
constexpr double kOpBolt = 1.0 / 0.6;
constexpr double kOpScopeDelay = 0.08;  // it has to scope in first

enum class PeekType { Static, Swing, Wide, Crouch, Jump, Jiggle, Hold, Count };

const char* PeekName(PeekType p) {
    switch (p) {
        case PeekType::Static: return "static peek";
        case PeekType::Swing: return "swing";
        case PeekType::Wide: return "wide swing";
        case PeekType::Crouch: return "crouch peek";
        case PeekType::Jump: return "jump peek";
        case PeekType::Jiggle: return "jiggle";
        case PeekType::Hold: return "angle hold";
        default: return "";
    }
}

// What enemies do at each difficulty.
struct SniperDifficulty {
    double weights[static_cast<int>(PeekType::Count)];  // chance of each peek type
    bool allSpots;          // false: only the two sides of the big wall
    double holdTime;        // seconds a peeker stays out after stopping
    bool shootsBack;
    double reactionMs;      // enemy reaction once it sees you
    double turnDegPerSec;
    double aimErrDeg;       // per-shot aim error (std dev)
    double headChance;
    double tapInterval;     // seconds between enemy rifle taps
    double opChance;        // chance an enemy carries an Operator (one body shot kills)
};

SniperDifficulty SniperDiff(Difficulty d) {
    switch (d) {
        //                 static swing wide crouch jump jiggle hold
        case Difficulty::Easy:
            return {{1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}, false, 2.4, false, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0};
        case Difficulty::Normal:
            return {{0.35, 0.45, 0.0, 0.20, 0.0, 0.0, 0.0}, true, 1.4, true, 650.0, 300.0, 1.3, 0.15, 0.45, 0.0};
        case Difficulty::Hard:
            return {{0.0, 0.25, 0.20, 0.15, 0.15, 0.10, 0.15}, true, 0.95, true, 420.0, 450.0, 0.8, 0.30, 0.30, 0.4};
        default:
            return {{0.0, 0.20, 0.25, 0.15, 0.15, 0.10, 0.15}, true, 0.70, true, 300.0, 600.0, 0.45, 0.45, 0.25, 0.6};
    }
}

// A place enemies peek from: where they hide, which way (+1 / -1 along x)
// they step out, how far it is to the cover's edge, and how far they can go.
struct PeekSpot {
    Vector3 hide;
    float dir;
    float edge;
    float maxOffset;
    const char* name;
};

const PeekSpot kSpots[4] = {
    {{-3.0f, 0.0f, -26.8f}, -1.0f, 1.5f, 5.4f, "left side of the wall"},
    {{3.0f, 0.0f, -26.8f}, 1.0f, 1.5f, 5.4f, "right side of the wall"},
    {{-7.4f, 0.0f, -14.2f}, 1.0f, 2.4f, 6.5f, "left box"},
    {{7.4f, 0.0f, -9.2f}, -1.0f, 2.4f, 6.5f, "right box"},
};

// Centre (clear view of every spot), or behind one of your two boxes.
const Vector3 kPlayerSpawns[3] = {{0.0f, 0.0f, 10.0f}, {-5.6f, 0.0f, 9.4f}, {5.6f, 0.0f, 7.9f}};

class SniperMode : public Mode {
public:
    explicit SniperMode(const GameContext& ctx)
        : Mode(ModeId::Sniper, ctx), spec_(GetSniperSpec(ctx.weapon)), diff_(SniperDiff(ctx.difficulty)) {
        useOnsetReaction_ = false;
        BuildArena();
        player_.pos = kPlayerSpawns[0];
        ctx_.cam->SetEye(player_.Eye());
    }
    ~SniperMode() override { ctx_.world->ClearCovers(); }
    SniperMode(const SniperMode&) = delete;
    SniperMode& operator=(const SniperMode&) = delete;

    void Begin(double t) override {
        ammo_ = spec_.magazine;
        readyAt_ = t;
        nextEventAt_ = t + 0.8;
    }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        const float fdt = static_cast<float>(dt);

        // Respawn after a death.
        if (dead_ && t >= respawnAt_) Respawn();

        if (reloading_ && t >= readyAt_) {
            reloading_ = false;
            ammo_ = spec_.magazine;
        }

        // Player movement (scoped you move slower).
        const float speedMult = static_cast<float>(spec_.speedMult) * (zoomLevel_ > 0 ? kScopedSpeedMult : 1.0f);
        StepPlayer(player_, ctx_.cam->Yaw(), !dead_, speedMult, fdt, moveBoxes_);
        ctx_.cam->SetEye(player_.Eye());

        // Next enemy: after a short gap, once the rifle is ready (so every
        // rifle gets the same number of peeks).
        if (!active_ && !dead_ && t >= nextEventAt_ && !reloading_ && t + 0.3 >= readyAt_) StartEvent(t);
        if (active_) UpdateEnemy(t, fdt);

        // Keep the drawable / hittable body in sync.
        targets_.clear();
        if (active_) targets_.push_back(BodyOf(bot_));

        // Reaction time / TTK start: the first moment you could see it.
        if (active_ && !dead_ && visibleSince_ < 0.0 && mv::CanSee(*ctx_.world, player_.Eye(), BodyOf(bot_))) {
            visibleSince_ = t;
        }
    }

    void OnShot(double t) override {
        if (dead_) return;
        if (reloading_ || t < readyAt_ || ammo_ <= 0) {
            ctx_.audio->Play(Sfx::UiClick);  // dry click: rifle not ready
            return;
        }
        --ammo_;
        readyAt_ = t + spec_.shotInterval;
        stats_.shots++;
        const bool moving = !player_.Accurate();
        if (moving) stats_.movingShots++;
        ctx_.audio->Play(Sfx::Shot);
        lastShot_.time = t;
        lastShot_.hit = false;
        lastShot_.head = false;
        lastShot_.points = 0;

        // First shot at this enemy: reaction time.
        if (active_ && visibleSince_ >= 0.0 && !reactionRecorded_) {
            const double ms = (t - visibleSince_) * 1000.0;
            if (ms > 80.0 && ms < 2000.0) {
                stats_.reactionSumMs += ms;
                stats_.reactionCount++;
            }
            reactionRecorded_ = true;
        }

        const Ray ray = ShotRay();
        const bool noScope = zoomLevel_ == 0;
        if (ammo_ <= 0) {
            reloading_ = true;
            readyAt_ = t + spec_.reloadTime;
            zoomLevel_ = 0;  // reloading drops the scope, like in Valorant
        }
        if (!active_) {
            streak_ = 0;
            return;
        }
        const Target body = BodyOf(bot_);
        const TargetHit h = RaycastTarget(body, ray);
        if (!h.hit || h.distance >= ctx_.world->RaycastCovers(ray)) {
            streak_ = 0;
            return;
        }
        const double dmg = h.head ? spec_.headDamage : (h.legs ? spec_.legDamage : spec_.bodyDamage);
        botHp_ -= dmg;
        stats_.hits++;
        if (h.head) stats_.headshots++;
        stats_.damageDealt += std::min(dmg, botHp_ + dmg);
        ++streak_;
        bestStreak_ = std::max(bestStreak_, streak_);
        lastShot_.hit = true;
        lastShot_.head = h.head;
        const Vector3 p = {ray.position.x + ray.direction.x * h.distance, ray.position.y + ray.direction.y * h.distance,
                           ray.position.z + ray.direction.z * h.distance};
        if (ctx_.fx) ctx_.fx->Burst(p, h.head ? Color{255, 236, 190, 255} : ctx_.targetColor, h.head ? 18 : 8, 3.5f, *ctx_.rng);

        if (botHp_ <= 0.0) {
            // Kill: base + headshot + speed (from when it became visible) + no-scope.
            const double exposed = visibleSince_ >= 0.0 ? t - visibleSince_ : 1.2;
            const long long pts = 300 + (h.head ? 100 : 0) +
                                  static_cast<long long>(std::lround(std::max(0.0, 1.0 - exposed / 1.2) * 200.0)) +
                                  (noScope ? 100 : 0);
            stats_.score += pts;
            lastShot_.points = pts;
            stats_.kills++;
            stats_.peeks++;
            if (visibleSince_ >= 0.0) {
                stats_.ttkSumMs += exposed * 1000.0;
                stats_.ttkCount++;
            }
            ctx_.audio->Play(h.head ? Sfx::Headshot : Sfx::Kill);
            message_ = h.head ? "HEADSHOT" : "KILL";
            messageUntil_ = t + 0.9;
            EndEvent(t);
        } else {
            // Hit but alive (a Marshal / Outlaw body shot): it falls back.
            const long long pts = static_cast<long long>(std::lround(dmg / 3.0));
            stats_.score += pts;
            lastShot_.points = pts;
            ctx_.audio->Play(Sfx::Hit);
            if (type_ != PeekType::Hold) phase_ = Phase::Back;
        }
    }

    void OnButton(int bindCode, bool down, double /*t*/) override {
        if (bindCode != ctx_.scopeBind || reloading_ || dead_) return;
        if (ctx_.scopeHold) {
            zoomLevel_ = down ? 1 : 0;
            return;
        }
        if (!down) return;
        const int levels = spec_.zoom2 > 0.0 ? 2 : 1;
        zoomLevel_ = (zoomLevel_ + 1) % (levels + 1);
    }

    double Zoom() const override {
        if (zoomLevel_ == 1) return spec_.zoom1;
        if (zoomLevel_ == 2) return spec_.zoom2;
        return 1.0;
    }

    bool HideCrosshair() const override { return zoomLevel_ > 0 || dead_; }

    bool OwnWorld() const override { return true; }
    Color SkyColor(float b) const override { return Shade(Color{62, 72, 92, 255}, b); }

    void Draw3D() const override {
        ctx_.world->DrawArenaFloor(9.5f, 37.0f, Color{92, 94, 100, 255});
        ctx_.world->DrawCovers();
        // Red line on the floor: the middle of the lane you can't cross.
        ctx_.world->DrawBoxLit(Vector3{0.0f, 0.01f, kPlayerMaxZ - 0.4f}, Vector3{18.0f, 0.02f, 0.08f}, Color{210, 64, 76, 255});
        Mode::Draw3D();
    }

    void DrawOverlay() const override {
        if (zoomLevel_ == 0 || dead_) return;
        // Scope: black outside a circle, thin black reticle inside.
        const Vector2 c = {static_cast<float>(GetScreenWidth()) * 0.5f, static_cast<float>(GetScreenHeight()) * 0.5f};
        const float r = static_cast<float>(GetScreenHeight()) * 0.47f;
        const float far = static_cast<float>(GetScreenWidth() + GetScreenHeight());
        DrawRing(c, r, far, 0.0f, 360.0f, 96, BLACK);
        DrawRing(c, r - 3.0f, r, 0.0f, 360.0f, 96, Color{20, 20, 20, 255});
        const float th = std::max(1.0f, ui::Scale());
        DrawLineEx(Vector2{c.x - r, c.y}, Vector2{c.x - 6.0f, c.y}, th, BLACK);
        DrawLineEx(Vector2{c.x + 6.0f, c.y}, Vector2{c.x + r, c.y}, th, BLACK);
        DrawLineEx(Vector2{c.x, c.y - r}, Vector2{c.x, c.y - 6.0f}, th, BLACK);
        DrawLineEx(Vector2{c.x, c.y + 6.0f}, Vector2{c.x, c.y + r}, th, BLACK);
        DrawCircleV(c, std::max(1.5f, 1.5f * ui::Scale()), Color{255, 70, 80, 255});
    }

    void DrawHud(double t) const override {
        const float vw = ui::VW(), vh = ui::VH();
        const float cx = vw * 0.5f, cy = vh * 0.5f;

        // Damage flash / death screen.
        const double hurt = t - lastHurtTime_;
        if (hurt >= 0.0 && hurt < 0.35) {
            ui::Fill(Rectangle{0.0f, 0.0f, vw, vh}, ui::Alpha(Color{200, 20, 30, 255}, static_cast<float>(0.35 * (1.0 - hurt / 0.35))));
        }
        if (dead_) {
            ui::Fill(Rectangle{0.0f, 0.0f, vw, vh}, ui::Alpha(ui::theme::kBg, 0.55f));
            ui::TextBold("YOU DIED", cx, cy - 90.0f, 60.0f, ui::theme::kAccent, ui::Align::Center);
            ui::Text(deathNote_, cx, cy - 20.0f, 22.0f, ui::theme::kText, ui::Align::Center);
            ui::Text(TextFormat("Respawning in %.1f", std::max(0.0, respawnAt_ - t)), cx, cy + 16.0f, 18.0f, ui::theme::kTextDim,
                     ui::Align::Center);
        } else {
            // Movement accuracy hint under the crosshair.
            const bool accurate = player_.Accurate();
            ui::Text(accurate ? "ACCURATE" : (player_.onGround ? "MOVING - SHOTS WILL WHIFF" : "AIRBORNE"), cx, cy + 40.0f, 14.0f,
                     ui::Alpha(accurate ? ui::theme::kGood : ui::theme::kAccent, 0.8f), ui::Align::Center);
        }
        if (active_ && type_ == PeekType::Hold && !dead_ && visibleSince_ < 0.0) {
            ui::TextBold("ENEMY HOLDING AN ANGLE", cx, 150.0f, 26.0f, ui::theme::kWarn, ui::Align::Center);
            ui::Text(TextFormat("Somewhere near the %s. Peek it, stop, then shoot.", kSpots[spot_].name), cx, 184.0f, 18.0f, ui::theme::kTextDim,
                     ui::Align::Center);
        }
        if (t < messageUntil_) {
            ui::TextBold(message_, cx, cy - 120.0f, 30.0f, message_[0] == 'H' ? ui::theme::kWarn : ui::theme::kText, ui::Align::Center);
        }

        // Health (bottom left, above the info bar).
        const Rectangle hp = {14.0f, vh - 130.0f, 300.0f, 80.0f};
        ui::Angled(hp, ui::Alpha(ui::theme::kBg, 0.8f), 12.0f);
        const double health = std::min(100.0, std::max(0.0, hp_));
        const double shield = std::max(0.0, hp_ - 100.0);
        ui::TextBold(TextFormat("%.0f", health), hp.x + 18.0f, hp.y + 10.0f, 34.0f, ui::theme::kText);
        ui::Text(TextFormat("SHIELD %.0f", shield), hp.x + 110.0f, hp.y + 20.0f, 18.0f, ui::theme::kTextDim);
        ui::Fill(Rectangle{hp.x + 18.0f, hp.y + 56.0f, 264.0f, 8.0f}, ui::theme::kPanel2);
        ui::Fill(Rectangle{hp.x + 18.0f, hp.y + 56.0f, 264.0f * static_cast<float>(health / 100.0), 8.0f},
                 health > 30.0 ? ui::theme::kText : ui::theme::kAccent);
        ui::Fill(Rectangle{hp.x + 18.0f, hp.y + 66.0f, 264.0f * static_cast<float>(shield / 50.0), 4.0f}, Color{90, 180, 255, 255});

        // Weapon panel (bottom right).
        const Rectangle p = {vw - 300.0f, vh - 130.0f, 286.0f, 100.0f};
        ui::Angled(p, ui::Alpha(ui::theme::kBg, 0.8f), 14.0f);
        ui::TextBold(spec_.name, p.x + 18.0f, p.y + 12.0f, 26.0f, ui::theme::kText);
        const char* zoomText = zoomLevel_ == 0 ? "UNSCOPED" : TextFormat("%.1fx", Zoom());
        ui::Text(zoomText, p.x + p.width - 18.0f, p.y + 18.0f, 18.0f, zoomLevel_ ? ui::theme::kGood : ui::theme::kTextDim,
                 ui::Align::Right);
        if (reloading_) {
            const float k = static_cast<float>(std::min(1.0, std::max(0.0, 1.0 - (readyAt_ - t) / spec_.reloadTime)));
            ui::Text("RELOADING", p.x + 18.0f, p.y + 52.0f, 20.0f, ui::theme::kWarn);
            ui::Fill(Rectangle{p.x + 18.0f, p.y + 80.0f, (p.width - 36.0f) * k, 5.0f}, ui::theme::kWarn);
        } else {
            for (int i = 0; i < spec_.magazine; ++i) {
                ui::Fill(Rectangle{p.x + 18.0f + static_cast<float>(i) * 22.0f, p.y + 54.0f, 14.0f, 28.0f},
                         i < ammo_ ? ui::theme::kText : ui::Alpha(ui::theme::kTextDim, 0.3f));
            }
            if (t < readyAt_) {
                const float k = static_cast<float>(std::min(1.0, std::max(0.0, 1.0 - (readyAt_ - t) / spec_.shotInterval)));
                ui::Fill(Rectangle{p.x + 18.0f, p.y + 88.0f, (p.width - 36.0f) * k, 3.0f}, ui::theme::kAccent);
            }
        }

        // K / D next to the score.
        ui::Text(TextFormat("K %d  /  D %d", stats_.kills, stats_.deaths), vw - 300.0f + 18.0f, 134.0f, 18.0f, ui::theme::kTextDim);
    }

protected:
    bool AimHead() const override { return true; }

    // Scoped + still = perfectly accurate. Unscoped adds the rifle's hipfire
    // spread; moving adds the movement error; in the air it is hopeless.
    Ray ShotRay() override {
        Ray ray;
        ray.position = player_.Eye();
        double spread = zoomLevel_ > 0 ? 0.0 : spec_.hipSpreadDeg;
        if (!player_.onGround) {
            spread += kAirSpread;
        } else if (player_.Speed() > kAccurateSpeed) {
            spread += spec_.moveSpreadDeg * std::min(1.0, static_cast<double>((player_.Speed() - kAccurateSpeed) / (kRunSpeed - kAccurateSpeed)));
        }
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double rad = spread * std::sqrt(ctx_.rng->Uniform(0.0, 1.0));
        ray.direction = DirectionFromAngles(ctx_.cam->Yaw() + std::cos(a) * rad, ctx_.cam->Pitch() + std::sin(a) * rad);
        return ray;
    }

private:
    enum class Phase { Wait, Out, Hold, Back };

    void BuildArena() {
        std::vector<Box> b;
        CoverStyle style = CoverStyle::Wall;
        auto add = [&](float x, float z, float sx, float sz, float h) {
            Box bx;
            bx.center = Vector3{x, h * 0.5f, z};
            bx.size = Vector3{sx, h, sz};
            bx.style = style;
            b.push_back(bx);
        };
        // The lane: 18 m wide, ~50 m long.
        add(-9.25f, -10.0f, 0.5f, 50.5f, 4.0f);
        add(9.25f, -10.0f, 0.5f, 50.5f, 4.0f);
        add(0.0f, 15.25f, 19.0f, 0.5f, 4.0f);
        add(0.0f, -35.25f, 19.0f, 0.5f, 4.0f);
        // Enemy side: the big wall (peeks from both of its sides) and two boxes.
        add(0.0f, -24.0f, 9.0f, 1.4f, 4.0f);
        style = CoverStyle::Crate;
        add(-6.6f, -12.0f, 3.2f, 2.0f, 2.6f);
        add(6.6f, -7.0f, 3.2f, 2.0f, 2.6f);
        // Your side: two tall boxes to peek from and a low one to crouch behind.
        add(-5.6f, 7.0f, 2.2f, 2.0f, 2.6f);
        add(5.6f, 5.5f, 2.2f, 2.0f, 2.6f);
        style = CoverStyle::Barrier;
        add(0.0f, 2.5f, 2.6f, 0.9f, 1.05f);
        ctx_.world->SetCovers(b);
        // You can't walk past the middle (an invisible wall that doesn't block shots).
        moveBoxes_ = b;
        moveBoxes_.push_back(Box{Vector3{0.0f, 2.0f, kPlayerMaxZ - 0.9f}, Vector3{18.0f, 4.0f, 1.0f}});
    }

    void Respawn() {
        dead_ = false;
        hp_ = kPlayerHp;
        const bool cover = ctx_.difficulty == Difficulty::Hard || ctx_.difficulty == Difficulty::Insane;
        player_ = Mover{};
        player_.pos = kPlayerSpawns[cover ? ctx_.rng->Int(0, 2) : 0];
        ctx_.cam->Reset(0.0, 0.0);
        ctx_.cam->SetEye(player_.Eye());
        ctx_.history->Clear();
    }

    PeekType PickType() {
        double total = 0.0;
        for (double w : diff_.weights) total += w;
        double r = ctx_.rng->Uniform(0.0, total);
        for (int i = 0; i < static_cast<int>(PeekType::Count); ++i) {
            r -= diff_.weights[i];
            if (r <= 0.0 && diff_.weights[i] > 0.0) return static_cast<PeekType>(i);
        }
        return PeekType::Static;
    }

    Vector3 SpotPos(int spot, float offset) const {
        const PeekSpot& s = kSpots[spot];
        return Vector3{s.hide.x + s.dir * offset, 0.0f, s.hide.z};
    }

    float Offset() const { return (bot_.pos.x - kSpots[spot_].hide.x) * kSpots[spot_].dir; }

    void StartEvent(double t) {
        active_ = true;
        type_ = PickType();
        spot_ = diff_.allSpots ? ctx_.rng->Int(0, 3) : ctx_.rng->Int(0, 1);
        const PeekSpot& s = kSpots[spot_];
        bot_ = Mover{};
        botHp_ = kEnemyHp;
        visibleSince_ = -1.0;
        reactionRecorded_ = false;
        seen_ = false;
        jumped_ = false;
        enemyOp_ = ctx_.rng->Chance(diff_.opChance);
        phase_ = Phase::Wait;
        phaseEnd_ = t + ctx_.rng->Uniform(0.2, ctx_.difficulty == Difficulty::Easy ? 0.9 : 1.2);

        // How far out it goes, per peek type.
        const float rnd = ctx_.rng->UniformF(0.0f, 1.0f);
        switch (type_) {
            case PeekType::Static: goal_ = s.edge + 1.2f + 0.6f * rnd; break;
            case PeekType::Swing: goal_ = s.edge + 1.0f + 0.8f * rnd; break;
            case PeekType::Wide: goal_ = s.edge + 3.2f + 0.8f * rnd; break;
            case PeekType::Crouch: goal_ = s.edge + 1.0f + 0.6f * rnd; break;
            case PeekType::Jiggle: goal_ = s.edge + 0.15f; break;
            default: goal_ = s.edge + 1.0f; break;
        }
        goal_ = std::min(goal_, s.maxOffset);
        bot_.pos = SpotPos(spot_, type_ == PeekType::Jump ? -1.2f : 0.0f);

        if (type_ == PeekType::Hold && !PlaceHolder(t)) {
            type_ = PeekType::Swing;  // you're in the open: nothing to hold, it swings instead
            goal_ = s.edge + 1.2f;
            bot_.pos = SpotPos(spot_, 0.0f);
        }
        // Face your side of the lane.
        AimAt(Vector3{player_.pos.x, 1.4f, player_.pos.z}, 1e9);
    }

    // Angle hold: stand somewhere you can't see from where you are now, aimed
    // near your cover. Returns false when there is no hidden spot.
    bool PlaceHolder(double t) {
        std::vector<std::pair<int, float>> options;
        for (int sp = 0; sp < 4; ++sp) {
            for (float extra : {0.5f, 1.2f, 2.0f}) {
                const float off = std::min(kSpots[sp].edge + extra, kSpots[sp].maxOffset);
                Mover m;
                m.pos = SpotPos(sp, off);
                if (!mv::CanSee(*ctx_.world, player_.Eye(), BodyOf(m))) options.emplace_back(sp, off);
            }
        }
        if (options.empty()) return false;
        const auto& o = options[static_cast<size_t>(ctx_.rng->Int(0, static_cast<int>(options.size()) - 1))];
        spot_ = o.first;
        bot_.pos = SpotPos(spot_, o.second);
        phase_ = Phase::Hold;
        phaseEnd_ = 0.0;
        holdTimeout_ = t + 8.0;
        // Pre-aim beside where you are (it has to adjust when you show up).
        preAim_ = Vector3{player_.pos.x + ctx_.rng->UniformF(-1.6f, 1.6f), 1.4f, player_.pos.z};
        return true;
    }

    void EndEvent(double t) {
        active_ = false;
        targets_.clear();
        nextEventAt_ = t + ctx_.rng->Uniform(0.9, 1.5);
    }

    // The enemy got away (went back behind cover or gave up the angle).
    void Escaped(double t) {
        if (type_ != PeekType::Jiggle) {  // a jiggle is bait, not a missed kill
            stats_.peeks++;
            stats_.expired++;
            stats_.score -= 50;
        }
        EndEvent(t);
    }

    void UpdateEnemy(double t, float dt) {
        const PeekSpot& s = kSpots[spot_];
        float wx = 0.0f, wz = 0.0f, speed = 0.0f;
        bool crouch = false;
        const float off = Offset();

        switch (phase_) {
            case Phase::Wait:
                if (t >= phaseEnd_) phase_ = Phase::Out;
                break;
            case Phase::Out: {
                const bool walk = type_ == PeekType::Static;
                speed = walk ? kWalkSpeed : kRunSpeed;
                wx = s.dir;
                if (type_ == PeekType::Jump) {
                    if (!jumped_ && off >= s.edge - 0.4f && bot_.onGround) {
                        bot_.onGround = false;
                        bot_.vel.y = kJumpSpeed;
                        jumped_ = true;
                    } else if (jumped_ && bot_.onGround) {
                        phase_ = Phase::Back;  // landed: straight back to cover
                    }
                    break;
                }
                // Brake (counter-strafe) so it stops right at the goal.
                const float v = bot_.Speed();
                const float brake = v * v / (2.0f * kAccel) + 0.05f;
                if (off >= goal_ - brake) {
                    mv::CounterStrafe(bot_, wx, wz, speed);
                    if (v < 0.3f) {
                        if (type_ == PeekType::Jiggle) {
                            phase_ = Phase::Back;
                        } else {
                            phase_ = Phase::Hold;
                            phaseEnd_ = t + diff_.holdTime * ctx_.rng->Uniform(0.8, 1.2);
                        }
                    }
                }
                break;
            }
            case Phase::Hold:
                crouch = type_ == PeekType::Crouch;
                if (type_ == PeekType::Hold) {
                    if (t >= holdTimeout_) {
                        Escaped(t);
                        return;
                    }
                } else if (t >= phaseEnd_) {
                    phase_ = Phase::Back;
                }
                break;
            case Phase::Back:
                wx = -s.dir;
                speed = kRunSpeed;
                crouch = false;
                if (off <= s.edge - 0.7f) {
                    Escaped(t);
                    return;
                }
                break;
        }
        Step(bot_, wx, wz, speed, crouch, dt, ctx_.world->Covers());
        EnemyCombat(t, dt);
    }

    // The enemy shoots back while it is standing still and can see you.
    void EnemyCombat(double t, float dt) {
        if (!diff_.shootsBack || dead_ || phase_ != Phase::Hold) {
            if (type_ == PeekType::Hold) AimAt(preAim_, 1e9);
            return;
        }
        const Target me = BodyOf(player_);
        const bool sees = mv::CanSee(*ctx_.world, bot_.Eye(), me);
        if (!sees) {
            if (type_ == PeekType::Hold) AimAt(preAim_, diff_.turnDegPerSec * dt);
            return;
        }
        if (!seen_) {
            seen_ = true;
            // Peeker's advantage: swinging out fast makes you harder to react to.
            const double extra = player_.Speed() > 4.0f ? 0.12 : 0.0;
            reactAt_ = t + diff_.reactionMs / 1000.0 * ctx_.rng->Uniform(0.85, 1.2) + extra + (enemyOp_ ? kOpScopeDelay : 0.0);
            nextTap_ = reactAt_;
            aimHead_ = ctx_.rng->Chance(diff_.headChance);
        }
        if (t < reactAt_) return;
        const Vector3 goal = TargetAimPoint(me, aimHead_);
        const double off = AimAt(goal, diff_.turnDegPerSec * dt);
        if (off < 1.0 && t >= nextTap_) EnemyTap(t, me);
    }

    // Turns the enemy's aim towards a point; returns the remaining angle.
    double AimAt(Vector3 point, double maxStep) {
        double gy = 0.0, gp = 0.0;
        AnglesFromDirection(Sub(point, bot_.Eye()), gy, gp);
        const double dy = val::NormalizeDeg(gy - botYaw_);
        const double dp = gp - botPitch_;
        const double len = std::hypot(dy, dp);
        if (len <= maxStep || len < 1e-9) {
            botYaw_ = gy;
            botPitch_ = gp;
            return 0.0;
        }
        botYaw_ = val::NormalizeDeg(botYaw_ + dy / len * maxStep);
        botPitch_ += dp / len * maxStep;
        return len - maxStep;
    }

    void EnemyTap(double t, const Target& me) {
        nextTap_ = t + (enemyOp_ ? kOpBolt : diff_.tapInterval * ctx_.rng->Uniform(0.9, 1.15));
        ctx_.audio->Play(Sfx::EnemyShot);
        // An Operator peeker takes its one shot and falls back behind cover.
        if (enemyOp_ && type_ != PeekType::Hold) phase_ = Phase::Back;
        // Harder to hit you while you move.
        const double err = diff_.aimErrDeg * (1.0 + 0.8 * std::min(1.0, static_cast<double>(player_.Speed()) / kRunSpeed));
        Ray ray;
        ray.position = bot_.Eye();
        ray.direction = DirectionFromAngles(botYaw_ + RandNormal(*ctx_.rng) * err, botPitch_ + RandNormal(*ctx_.rng) * err * 0.6);
        const TargetHit h = RaycastTarget(me, ray);
        if (!h.hit || h.distance >= ctx_.world->RaycastCovers(ray)) return;
        const double dmg = enemyOp_ ? (h.head ? kOpHead : (h.legs ? kOpLegs : kOpBody))
                                    : (h.head ? kRifleHead : (h.legs ? kRifleLegs : kRifleBody));
        hp_ -= dmg;
        stats_.damageTaken += dmg;
        lastHurtTime_ = t;
        ctx_.audio->Play(Sfx::Hurt);
        if (hp_ <= 0.0) {
            dead_ = true;
            respawnAt_ = t + kRespawnDelay;
            stats_.deaths++;
            stats_.peeks++;
            stats_.score -= 300;
            zoomLevel_ = 0;
            deathNote_ = std::string("Killed from the ") + kSpots[spot_].name + " (" + PeekName(type_) +
                         (enemyOp_ ? ", Operator" : "") + (h.head ? ", headshot)" : ")");
            EndEvent(t);
            nextEventAt_ = respawnAt_ + 0.8;
        }
    }

    SniperSpec spec_;
    SniperDifficulty diff_;
    std::vector<Box> moveBoxes_;

    // Player.
    Mover player_;
    double hp_ = kPlayerHp;
    bool dead_ = false;
    double respawnAt_ = 0.0;
    std::string deathNote_;
    int ammo_ = 5;
    bool reloading_ = false;
    double readyAt_ = 0.0;
    int zoomLevel_ = 0;
    double lastHurtTime_ = -10.0;
    std::string message_ = "KILL";
    double messageUntil_ = -1.0;

    // The current enemy.
    bool active_ = false;
    PeekType type_ = PeekType::Static;
    int spot_ = 0;
    Phase phase_ = Phase::Wait;
    double phaseEnd_ = 0.0;
    double holdTimeout_ = 0.0;
    float goal_ = 0.0f;
    bool jumped_ = false;
    Mover bot_;
    double botHp_ = kEnemyHp;
    double botYaw_ = 180.0;
    double botPitch_ = 0.0;
    Vector3 preAim_ = {0.0f, 1.4f, 8.0f};
    bool seen_ = false;
    bool enemyOp_ = false;
    bool aimHead_ = false;
    double reactAt_ = 0.0;
    double nextTap_ = 0.0;
    double visibleSince_ = -1.0;
    bool reactionRecorded_ = false;
    double nextEventAt_ = 0.0;
};

}  // namespace

std::unique_ptr<Mode> CreateSniperMode(const GameContext& ctx) { return std::make_unique<SniperMode>(ctx); }
