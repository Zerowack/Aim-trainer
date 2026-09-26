// vsbot.cpp - VS Bot: a 1v1 Sheriff duel on a small skirmish map with
// Valorant-style movement and a bot whose skill scales from Iron to Radiant.
//
// The map is point-symmetric (both sides are identical when turned 180
// degrees), both spawns sit behind a wall, and the bot walks a navigation
// graph so it goes around walls instead of into them.
//
// Weapon and movement numbers are Valorant's where they are published
// (Sheriff damage, fire rate, magazine, reload, 5.4 m/s, the 27.5% accuracy
// threshold, ~70 ms counter-strafe) and close approximations otherwise.
#include <algorithm>
#include <cmath>
#include <queue>
#include <string>
#include <vector>

#include "modes.h"
#include "movement.h"
#include "rank.h"
#include "rlgl.h"
#include "ui.h"

namespace {

using namespace mv;

// ---- Sheriff -------------------------------------------------------------------
constexpr double kFireInterval = 1.0 / 4.0;   // 4 rounds / s
constexpr int kMagazine = 6;
constexpr double kReloadTime = 2.25;
constexpr double kHeadDamage = 159.0;         // 0-30 m: a headshot kills through heavy shields
constexpr double kBodyDamage = 55.0;
constexpr double kLegDamage = 46.0;
constexpr double kHeadDamageFar = 145.0;      // 30-50 m
constexpr double kBodyDamageFar = 50.0;
constexpr double kLegDamageFar = 42.0;
constexpr double kFalloffRange = 30.0;
constexpr double kMaxHp = 150.0;              // 100 health + 50 heavy shields
constexpr double kBaseSpread = 0.25;          // standing first shot (degrees)
constexpr double kRunSpread = 3.0;            // extra at full running speed
constexpr double kAirSpread = 5.0;
constexpr double kBloomPerShot = 1.1;         // spread added per shot...
constexpr double kBloomMax = 4.0;
constexpr double kBloomRecover = 4.5;         // ...and recovered per second after a short delay
constexpr double kPunchPerShot = 2.2;         // view kick (degrees)
constexpr double kPunchRecover = 10.0;        // 1/s, exponential
constexpr double kRecoverDelay = 0.07;

// ---- Match -----------------------------------------------------------------------
constexpr int kWinRounds = 5;
constexpr double kRoundTime = 60.0;
constexpr double kFreezeTime = 3.0;
constexpr double kEndTime = 2.5;
constexpr float kHalfX = 15.0f;
constexpr float kHalfZ = 21.0f;

struct BotSkill {
    double reactionMs;
    double turnDegPerSec;
    double aimErrDeg;       // aim error (std-dev-ish) when it fires
    double headChance;
    double stopChance;      // counter-strafes before shooting
    int tapsMin, tapsMax;   // shots per burst
    double strafeChance;    // ADAD between bursts
    double crouchChance;    // crouches while shooting
    double correctRate;     // how fast it corrects its aim error onto you (1/s)
    double tapGapMin, tapGapMax;  // seconds between taps (discipline: wait for the spread to reset)
    double pauseMin, pauseMax;    // seconds between bursts
};

BotSkill SkillFor(int tier) {
    static const BotSkill table[kTierCount] = {
        //  react  turn  err   head  stop  taps  strafe crouch correct  tap gap     pause
        {540.0, 100.0, 3.20, 0.06, 0.10, 3, 6, 0.05, 0.05, 0.3, 0.25, 0.30, 0.55, 1.00},  // Iron
        {450.0, 150.0, 2.30, 0.14, 0.25, 2, 5, 0.10, 0.08, 0.7, 0.26, 0.34, 0.45, 0.85},  // Bronze
        {380.0, 210.0, 1.70, 0.24, 0.40, 2, 4, 0.20, 0.10, 1.1, 0.28, 0.38, 0.35, 0.70},  // Silver
        {320.0, 290.0, 1.25, 0.35, 0.55, 2, 4, 0.30, 0.12, 1.8, 0.30, 0.42, 0.28, 0.55},  // Gold
        {280.0, 370.0, 0.95, 0.45, 0.70, 1, 3, 0.40, 0.15, 2.5, 0.33, 0.45, 0.22, 0.48},  // Platinum
        {245.0, 450.0, 0.72, 0.55, 0.80, 1, 3, 0.50, 0.18, 3.3, 0.35, 0.48, 0.18, 0.42},  // Diamond
        {215.0, 550.0, 0.55, 0.68, 0.90, 1, 2, 0.60, 0.20, 4.2, 0.37, 0.50, 0.14, 0.36},  // Ascendant
        {190.0, 670.0, 0.40, 0.80, 0.95, 1, 2, 0.70, 0.22, 5.2, 0.40, 0.52, 0.11, 0.30},  // Immortal
        {165.0, 820.0, 0.28, 0.90, 1.00, 1, 2, 0.80, 0.25, 6.5, 0.42, 0.55, 0.08, 0.25},  // Radiant
    };
    return table[std::max(0, std::min(kTierCount - 1, tier))];
}

// Spread from movement (degrees): 0 when accurate.
double MovementError(const Mover& m) {
    if (!m.onGround) return kAirSpread;
    const double sp = m.Speed();
    if (sp <= kAccurateSpeed) return 0.0;
    return kRunSpread * std::min(1.0, (sp - kAccurateSpeed) / (kRunSpeed - kAccurateSpeed));
}

// Total first-shot spread for a mover with some bloom.
double ShotSpread(const Mover& m, double bloom) {
    double s = kBaseSpread + MovementError(m) + bloom;
    if (m.crouch > 0.9f && m.Accurate()) s *= 0.85;  // crouched and still: a little tighter
    return s;
}

// Pistol state shared by the player and the bot: bloom and view kick
// grow with each shot and recover shortly after.
struct Gun {
    int ammo = kMagazine;
    bool reloading = false;
    double readyAt = 0.0;
    double lastShot = -10.0;
    double bloom = 0.0;
    double punchPitch = 0.0;
    double punchYaw = 0.0;

    void Recover(double t, double dt) {
        if (t - lastShot < kRecoverDelay) return;
        bloom = std::max(0.0, bloom - kBloomRecover * dt);
        const double k = std::exp(-kPunchRecover * dt);
        punchPitch *= k;
        punchYaw *= k;
    }
};

// ---- Map + navigation --------------------------------------------------------------
struct NavNode {
    Vector3 p;
    bool hold;            // a good spot to hold an angle from
    std::vector<int> links;
};

Vector3 Add3(Vector3 a, Vector3 b) { return Vector3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Scale3(Vector3 a, float k) { return Vector3{a.x * k, a.y * k, a.z * k}; }
Vector3 Cross(Vector3 a, Vector3 b) { return Vector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

class VsBotMode : public Mode {
public:
    explicit VsBotMode(const GameContext& ctx) : Mode(ModeId::VsBot, ctx), skill_(SkillFor(ctx.botTier)) {
        stats_.botTier = ctx.botTier;
        useOnsetReaction_ = false;
        // Build the map right away so the pre-match countdown already shows
        // it from the spawn.
        BuildArena();
        ctx_.cam->SetEye(Vector3{0.0f, kStandEye, 17.5f});
    }
    ~VsBotMode() override {
        ctx_.world->ClearCovers();
        ctx_.cam->SetPunch(0.0, 0.0);
    }
    VsBotMode(const VsBotMode&) = delete;
    VsBotMode& operator=(const VsBotMode&) = delete;

    void Begin(double t) override { StartRound(t); }

    bool Finished() const override { return phase_ == Phase::MatchOver; }
    bool OwnWorld() const override { return true; }
    Color SkyColor(float b) const override { return Shade(Color{62, 72, 92, 255}, b); }

    bool HudTimer(double t, double& secondsLeft, std::string& label) const override {
        secondsLeft = phase_ == Phase::Live ? std::max(0.0, roundEnd_ - t) : kRoundTime;
        label = TextFormat("YOU %d  :  %d BOT (%s)", stats_.roundsWon, stats_.roundsLost, TierName(ctx_.botTier));
        return true;
    }

    void CrosshairError(double /*t*/, double& firingDeg, double& movingDeg) const override {
        firingDeg = gun_.bloom;
        movingDeg = MovementError(player_);
    }

    void OnShot(double t) override { Fire(t); }

    void Update(double t, double dt, bool /*triggerHeld*/) override {
        TickTargets(dt);
        const float fdt = static_cast<float>(dt);

        // Phases.
        if (phase_ == Phase::Freeze && t >= phaseEnd_) {
            phase_ = Phase::Live;
            roundEnd_ = t + kRoundTime;
            ctx_.audio->Play(Sfx::CountGo);
        } else if (phase_ == Phase::Live && t >= roundEnd_) {
            EndRound(t, 0);  // time out: nobody scores
        } else if (phase_ == Phase::RoundOver && t >= phaseEnd_) {
            if (stats_.roundsWon >= kWinRounds || stats_.roundsLost >= kWinRounds) phase_ = Phase::MatchOver;
            else StartRound(t);
        }

        const bool live = phase_ == Phase::Live;
        UpdatePlayer(t, fdt, live);
        UpdateBot(t, fdt, live);

        // Keep the bot's drawable / hittable body in sync (pose included).
        if (!targets_.empty()) {
            Target& b = targets_[0];
            b.pos = bot_.pos;
            b.crouch = bot_.crouch;
            b.yaw = static_cast<float>(botYaw_);
            b.moveSpeed = bot_.Speed();
            b.walkPhase = botWalk_;
            b.airborne = bot_.onGround ? 0.0f : 1.0f;
        }
        stats_.score = std::max<long long>(0, stats_.roundsWon * 1000 - stats_.roundsLost * 300 +
                                                  static_cast<long long>(std::lround(stats_.damageDealt)));
    }

    void Draw3D() const override {
        const World& w = *ctx_.world;
        w.DrawArenaFloor(kHalfX, kHalfZ, Color{92, 94, 100, 255});
        // Spawn zones: a painted border, blue for you and red for the bot.
        auto zone = [&](float z, Color c) {
            const Color k = Shade(c, ctx_.brightness);
            w.DrawBoxLit(Vector3{0.0f, 0.004f, z - 3.1f}, Vector3{14.0f, 0.006f, 0.12f}, k, Surface::Plain);
            w.DrawBoxLit(Vector3{0.0f, 0.004f, z + 3.1f}, Vector3{14.0f, 0.006f, 0.12f}, k, Surface::Plain);
            w.DrawBoxLit(Vector3{-7.0f, 0.004f, z}, Vector3{0.12f, 0.006f, 6.2f}, k, Surface::Plain);
            w.DrawBoxLit(Vector3{7.0f, 0.004f, z}, Vector3{0.12f, 0.006f, 6.2f}, k, Surface::Plain);
            // Floor inside the zone: the normal floor with a light tint.
            const Color tint = {static_cast<unsigned char>((92 * 3 + c.r) / 4), static_cast<unsigned char>((94 * 3 + c.g) / 4),
                                static_cast<unsigned char>((100 * 3 + c.b) / 4), 255};
            w.DrawBoxLit(Vector3{0.0f, 0.003f, z}, Vector3{14.0f, 0.004f, 6.2f}, Shade(tint, ctx_.brightness), Surface::Floor);
        };
        zone(17.6f, Color{70, 130, 220, 255});
        zone(-17.6f, Color{220, 70, 80, 255});
        // Centre line.
        w.DrawBoxLit(Vector3{0.0f, 0.005f, 0.0f}, Vector3{kHalfX * 2.0f, 0.006f, 0.08f},
                     Shade(Color{200, 200, 205, 255}, ctx_.brightness), Surface::Plain);
        w.DrawCovers();
        if (botHp_ > 0.0) Mode::Draw3D();
        DrawViewModel();
    }

    void DrawHud(double t) const override {
        const float vw = ui::VW(), vh = ui::VH();
        const float cx = vw * 0.5f, cy = vh * 0.5f;

        // Damage flash + a marker pointing to where the shot came from.
        const double hurt = t - lastHurtTime_;
        if (hurt >= 0.0 && hurt < 0.35) {
            const float a = static_cast<float>(0.3 * (1.0 - hurt / 0.35));
            ui::Fill(Rectangle{0.0f, 0.0f, vw, vh}, ui::Alpha(Color{200, 20, 30, 255}, a));
        }
        if (hurt >= 0.0 && hurt < 1.0) {
            double yaw = 0.0, pitch = 0.0;
            AnglesFromDirection(Sub(hurtFrom_, player_.Eye()), yaw, pitch);
            const double rel = val::DegToRad(val::NormalizeDeg(yaw - ctx_.cam->Yaw()));
            const float r = 120.0f;
            const float px = cx + static_cast<float>(std::sin(rel)) * r, py = cy - static_cast<float>(std::cos(rel)) * r;
            ui::Fill(Rectangle{px - 6.0f, py - 6.0f, 12.0f, 12.0f}, ui::Alpha(Color{255, 70, 80, 255}, static_cast<float>(1.0 - hurt)));
        }

        // Movement accuracy hint under the crosshair.
        if (phase_ == Phase::Live) {
            const bool accurate = player_.Accurate();
            ui::Text(accurate ? "ACCURATE" : (player_.onGround ? "MOVING" : "AIRBORNE"), cx, cy + 38.0f, 14.0f,
                     ui::Alpha(accurate ? ui::theme::kGood : ui::theme::kAccent, 0.75f), ui::Align::Center);
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

        // Ammo (bottom right).
        const Rectangle am = {vw - 300.0f, vh - 130.0f, 286.0f, 80.0f};
        ui::Angled(am, ui::Alpha(ui::theme::kBg, 0.8f), 12.0f);
        ui::TextBold(TextFormat("%d", gun_.ammo), am.x + 18.0f, am.y + 10.0f, 34.0f,
                     gun_.ammo > 2 ? ui::theme::kText : ui::theme::kAccent);
        ui::Text(TextFormat("/ %d   SHERIFF", kMagazine), am.x + 64.0f, am.y + 22.0f, 18.0f, ui::theme::kTextDim);
        if (gun_.reloading) {
            const float k = static_cast<float>(std::min(1.0, std::max(0.0, 1.0 - (gun_.readyAt - t) / kReloadTime)));
            ui::Text("RELOADING", am.x + 18.0f, am.y + 52.0f, 16.0f, ui::theme::kWarn);
            ui::Fill(Rectangle{am.x + 130.0f, am.y + 58.0f, 136.0f * k, 5.0f}, ui::theme::kWarn);
        } else {
            ui::Text("R reload", am.x + 18.0f, am.y + 52.0f, 16.0f, ui::Alpha(ui::theme::kTextDim, 0.7f));
        }

        // K / D next to the score.
        ui::Text(TextFormat("K %d  /  D %d", stats_.kills, stats_.deaths), vw - 300.0f + 18.0f, 134.0f, 18.0f, ui::theme::kTextDim);

        // Round messages.
        if (phase_ == Phase::Freeze) {
            const int n = static_cast<int>(std::ceil(phaseEnd_ - t));
            ui::TextBold(TextFormat("ROUND %d", round_), cx, cy - 170.0f, 54.0f, ui::theme::kText, ui::Align::Center);
            ui::Text(TextFormat("Starts in %d  -  WASD move, Shift walk, Ctrl crouch, Space jump, R reload", std::max(1, n)), cx,
                     cy - 104.0f, 20.0f, ui::theme::kTextDim, ui::Align::Center);
        } else if (phase_ == Phase::RoundOver || phase_ == Phase::MatchOver) {
            const bool won = lastRoundResult_ > 0;
            const char* head = lastRoundResult_ == 0 ? "TIME'S UP" : (won ? "ROUND WON" : "ROUND LOST");
            if (phase_ == Phase::MatchOver || stats_.roundsWon >= kWinRounds || stats_.roundsLost >= kWinRounds) {
                head = stats_.roundsWon > stats_.roundsLost ? "VICTORY" : "DEFEAT";
            }
            ui::TextBold(head, cx, cy - 170.0f, 60.0f, won ? ui::theme::kGood : ui::theme::kAccent, ui::Align::Center);
            ui::Text(roundNote_, cx, cy - 98.0f, 20.0f, ui::theme::kText, ui::Align::Center);
        }
    }

private:
    enum class Phase { Freeze, Live, RoundOver, MatchOver };
    enum class BotState { Moving, Holding, Engaging };

    // ------------------------------------------------------------------- map
    void BuildArena() {
        std::vector<Box> b;
        const Color outerTint = {118, 122, 132, 255};
        const Color spawnTint = {150, 156, 170, 255};
        const Color none = {0, 0, 0, 0};
        auto box = [&](float x, float z, float sx, float sz, float h, CoverStyle st, Color tint) {
            Box bx;
            bx.center = Vector3{x, h * 0.5f, z};
            bx.size = Vector3{sx, h, sz};
            bx.style = st;
            bx.tint = tint;
            b.push_back(bx);
        };
        // Point-symmetric pair: (x, z) and (-x, -z), so both sides are the same map.
        auto pair = [&](float x, float z, float sx, float sz, float h, CoverStyle st, Color tint) {
            box(x, z, sx, sz, h, st, tint);
            box(-x, -z, sx, sz, h, st, tint);
        };
        // Outer walls.
        box(-kHalfX - 0.3f, 0.0f, 0.6f, kHalfZ * 2.0f + 1.2f, 4.5f, CoverStyle::Wall, outerTint);
        box(kHalfX + 0.3f, 0.0f, 0.6f, kHalfZ * 2.0f + 1.2f, 4.5f, CoverStyle::Wall, outerTint);
        box(0.0f, -kHalfZ - 0.3f, kHalfX * 2.0f, 0.6f, 4.5f, CoverStyle::Wall, outerTint);
        box(0.0f, kHalfZ + 0.3f, kHalfX * 2.0f, 0.6f, 4.5f, CoverStyle::Wall, outerTint);
        // Spawn walls: each spawn sits behind a wall with two side wings.
        pair(0.0f, 14.0f, 10.0f, 0.8f, 3.4f, CoverStyle::Wall, spawnTint);
        pair(-7.4f, 16.8f, 0.8f, 3.6f, 3.4f, CoverStyle::Wall, spawnTint);
        pair(7.4f, 16.8f, 0.8f, 3.6f, 3.4f, CoverStyle::Wall, spawnTint);
        // Lane walls between mid and the side lanes (open in the middle).
        pair(-8.6f, 6.5f, 0.8f, 7.0f, 3.6f, CoverStyle::Wall, none);
        pair(8.6f, 6.5f, 0.8f, 7.0f, 3.6f, CoverStyle::Wall, none);
        // Mid: a big crate in the centre with a small one beside it.
        box(0.0f, 0.0f, 3.0f, 3.0f, 2.6f, CoverStyle::Crate, none);
        pair(2.3f, 1.9f, 1.4f, 1.4f, 1.2f, CoverStyle::Crate, none);
        // Low barrier you can crouch behind (your head shows when standing).
        pair(-2.6f, 7.6f, 3.0f, 0.6f, 1.1f, CoverStyle::Barrier, none);
        pair(3.2f, 9.4f, 1.8f, 1.8f, 2.0f, CoverStyle::Crate, none);
        // Left lane.
        pair(-12.0f, 11.0f, 2.0f, 2.0f, 2.0f, CoverStyle::Crate, none);
        pair(-11.6f, 4.2f, 2.8f, 0.6f, 1.1f, CoverStyle::Barrier, none);
        pair(-12.8f, 0.9f, 1.6f, 1.6f, 1.6f, CoverStyle::Crate, none);
        // Right lane.
        pair(12.0f, 7.4f, 2.2f, 2.2f, 2.6f, CoverStyle::Crate, none);
        pair(10.6f, 1.6f, 1.0f, 1.0f, 3.6f, CoverStyle::Wall, Color{170, 150, 120, 255});
        ctx_.world->SetCovers(b);
        BuildNav();
    }

    void BuildNav() {
        nodes_.clear();
        auto node = [&](float x, float z, bool hold) {
            nodes_.push_back(NavNode{Vector3{x, 0.0f, z}, hold, {}});
            if (std::fabs(x) > 0.01f || std::fabs(z) > 0.01f) nodes_.push_back(NavNode{Vector3{-x, 0.0f, -z}, hold, {}});
        };
        node(0.0f, 17.5f, false);  // spawn
        node(-4.0f, 17.8f, false);
        node(4.0f, 17.8f, false);
        node(-6.2f, 12.2f, true);  // spawn exits
        node(6.2f, 12.2f, true);
        node(-11.5f, 14.0f, false);  // left lane
        node(-10.5f, 8.0f, true);
        node(-11.0f, 2.2f, true);
        node(-6.5f, 1.0f, false);  // mid
        node(-3.2f, 4.0f, true);
        node(0.0f, 4.2f, false);
        node(3.8f, 4.5f, true);
        node(0.0f, 11.0f, false);
        node(-2.6f, 9.0f, true);  // behind the low barrier
        node(6.6f, 7.0f, false);
        node(11.5f, 12.5f, false);  // right lane
        node(12.8f, 4.0f, true);
        node(10.0f, 10.5f, true);
        // Connect every pair that can walk straight to each other.
        for (size_t i = 0; i < nodes_.size(); ++i) {
            for (size_t j = i + 1; j < nodes_.size(); ++j) {
                if (Len(Sub(nodes_[i].p, nodes_[j].p)) > 14.0f) continue;
                if (!Walkable(nodes_[i].p, nodes_[j].p)) continue;
                nodes_[i].links.push_back(static_cast<int>(j));
                nodes_[j].links.push_back(static_cast<int>(i));
            }
        }
    }

    // A mover can walk the straight line a -> b without touching a box.
    bool Walkable(Vector3 a, Vector3 b) const {
        const Vector3 d = Sub(b, a);
        const float len = std::sqrt(d.x * d.x + d.z * d.z);
        const int steps = std::max(1, static_cast<int>(len / 0.2f));
        const float clear = kRadius + 0.08f;
        for (int s = 0; s <= steps; ++s) {
            const float k = static_cast<float>(s) / static_cast<float>(steps);
            const float px = a.x + d.x * k, pz = a.z + d.z * k;
            for (const Box& bx : ctx_.world->Covers()) {
                const float hx = bx.size.x * 0.5f, hz = bx.size.z * 0.5f;
                const float qx = std::max(bx.center.x - hx, std::min(px, bx.center.x + hx));
                const float qz = std::max(bx.center.z - hz, std::min(pz, bx.center.z + hz));
                if ((px - qx) * (px - qx) + (pz - qz) * (pz - qz) < clear * clear) return false;
            }
        }
        return true;
    }

    int NearestNode(Vector3 p, bool mustReach) const {
        int best = -1;
        float bestD = 1e9f;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const float d = Len(Sub(nodes_[i].p, Vector3{p.x, 0.0f, p.z}));
            if (d < bestD && (!mustReach || Walkable(p, nodes_[i].p))) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        if (best < 0) return NearestNode(p, false);
        return best;
    }

    // Shortest path (Dijkstra) from the bot's position to node 'goal'.
    void PathTo(int goal) {
        path_.clear();
        stuckTime_ = 0.0;
        if (nodes_.empty()) return;
        const int start = NearestNode(bot_.pos, true);
        const size_t n = nodes_.size();
        std::vector<float> dist(n, 1e9f);
        std::vector<int> prev(n, -1);
        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
        dist[static_cast<size_t>(start)] = 0.0f;
        open.push({0.0f, start});
        while (!open.empty()) {
            const Item it = open.top();
            open.pop();
            const int u = it.second;
            if (it.first > dist[static_cast<size_t>(u)]) continue;
            if (u == goal) break;
            for (int v : nodes_[static_cast<size_t>(u)].links) {
                const float nd = dist[static_cast<size_t>(u)] +
                                 Len(Sub(nodes_[static_cast<size_t>(u)].p, nodes_[static_cast<size_t>(v)].p));
                if (nd < dist[static_cast<size_t>(v)]) {
                    dist[static_cast<size_t>(v)] = nd;
                    prev[static_cast<size_t>(v)] = u;
                    open.push({nd, v});
                }
            }
        }
        if (dist[static_cast<size_t>(goal)] > 1e8f) goal = start;  // unreachable: go to the nearest node
        for (int v = goal; v != -1; v = prev[static_cast<size_t>(v)]) {
            path_.insert(path_.begin(), v);
            if (v == start) break;
        }
    }

    // ----------------------------------------------------------------- rounds
    void StartRound(double t) {
        ++round_;
        phase_ = Phase::Freeze;
        phaseEnd_ = t + kFreezeTime;
        roundStart_ = t + kFreezeTime;
        lastUpdate_ = t;
        nextHunt_ = 0.0;
        player_ = Mover{};
        player_.pos = Vector3{ctx_.rng->UniformF(-3.0f, 3.0f), 0.0f, 17.5f};
        bot_ = Mover{};
        bot_.pos = Vector3{ctx_.rng->UniformF(-3.0f, 3.0f), 0.0f, -17.5f};
        hp_ = kMaxHp;
        botHp_ = kMaxHp;
        gun_ = Gun{};
        botGun_ = Gun{};
        viewKick_ = 0.0;
        botState_ = BotState::Moving;
        PickGoal();
        botYaw_ = 180.0;  // facing the player's side
        botPitch_ = 0.0;
        engaged_ = false;
        botVisibleSince_ = -1.0;
        reactionRecorded_ = false;
        botKnowsTime_ = -100.0;
        ctx_.cam->Reset(0.0, 0.0);
        ctx_.cam->SetEye(player_.Eye());
        ctx_.history->Clear();
        targets_.clear();
        targets_.push_back(BodyOf(bot_));
    }

    void EndRound(double t, int result, const std::string& note = "") {
        if (phase_ != Phase::Live) return;
        lastRoundResult_ = result;
        if (result > 0) stats_.roundsWon++;
        if (result < 0) stats_.roundsLost++;
        roundNote_ = note.empty() ? (result == 0 ? "Nobody scores this round." : "") : note;
        phase_ = Phase::RoundOver;
        phaseEnd_ = t + kEndTime;
    }

    // ---------------------------------------------------------------- player
    void UpdatePlayer(double t, float dt, bool live) {
        StepPlayer(player_, ctx_.cam->Yaw(), live, 1.0f, dt, ctx_.world->Covers());
        if (live && IsKeyPressed(KEY_R) && !gun_.reloading && gun_.ammo < kMagazine) StartReload(gun_, t);
        ctx_.cam->SetEye(player_.Eye());

        if (gun_.reloading && t >= gun_.readyAt) {
            gun_.reloading = false;
            gun_.ammo = kMagazine;
        }
        gun_.Recover(t, dt);
        ctx_.cam->SetPunch(gun_.punchYaw, gun_.punchPitch);
        viewKick_ = std::max(0.0, viewKick_ - static_cast<double>(dt) * 6.0);

        // When did the bot become visible to the player (reaction / TTK)?
        if (live && botHp_ > 0.0) {
            const Target body = BodyOf(bot_);
            const bool onScreen = AngleToPoint(*ctx_.cam, TargetAimPoint(body, false)) < 55.0;
            if (onScreen && CanSee(player_.Eye(), body)) {
                if (botVisibleSince_ < 0.0) botVisibleSince_ = t;
            } else {
                botVisibleSince_ = -1.0;
                reactionRecorded_ = false;
            }
        }
    }

    static void StartReload(Gun& g, double t) {
        g.reloading = true;
        g.readyAt = t + kReloadTime;
    }

    // Damage at a distance (Sheriff falloff past 30 m).
    static double Damage(const TargetHit& h) {
        const bool far = h.distance > kFalloffRange;
        if (h.head) return far ? kHeadDamageFar : kHeadDamage;
        if (h.legs) return far ? kLegDamageFar : kLegDamage;
        return far ? kBodyDamageFar : kBodyDamage;
    }

    // The Sheriff is semi-automatic: one shot per click.
    void Fire(double t) {
        if (phase_ != Phase::Live || gun_.reloading || t < gun_.readyAt) return;
        if (gun_.ammo <= 0) {
            StartReload(gun_, t);
            return;
        }
        // The bullet goes where the crosshair is (view kick included), plus spread.
        const bool moving = !player_.Accurate();
        const double spread = ShotSpread(player_, gun_.bloom);
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double r = spread * std::sqrt(ctx_.rng->Uniform(0.0, 1.0));
        Ray ray;
        ray.position = player_.Eye();
        ray.direction = DirectionFromAngles(ctx_.cam->Yaw() + std::cos(a) * r, ctx_.cam->Pitch() + std::sin(a) * r);

        stats_.shots++;
        if (moving) stats_.movingShots++;
        ctx_.audio->Play(Sfx::Shot);
        --gun_.ammo;
        gun_.lastShot = t;
        gun_.readyAt = t + kFireInterval;
        gun_.bloom = std::min(kBloomMax, gun_.bloom + kBloomPerShot);
        gun_.punchPitch = std::min(8.0, gun_.punchPitch + kPunchPerShot + ctx_.rng->Uniform(0.0, 0.4));
        gun_.punchYaw += ctx_.rng->Uniform(-0.5, 0.5);
        viewKick_ = 1.0;
        if (gun_.ammo <= 0) StartReload(gun_, t);

        if (botVisibleSince_ >= 0.0 && !reactionRecorded_) {
            const double ms = (t - botVisibleSince_) * 1000.0;
            if (ms > 60.0 && ms < 2000.0) {
                stats_.reactionSumMs += ms;
                stats_.reactionCount++;
            }
            reactionRecorded_ = true;
        }

        lastShot_.time = t;
        lastShot_.hit = false;
        lastShot_.head = false;
        lastShot_.points = 0;
        if (botHp_ <= 0.0) return;
        const Target body = BodyOf(bot_);
        const TargetHit h = RaycastTarget(body, ray);
        if (!h.hit || h.distance >= ctx_.world->RaycastCovers(ray)) {
            streak_ = 0;
            return;
        }
        const double dmg = Damage(h);
        botHp_ -= dmg;
        stats_.hits++;
        if (h.head) stats_.headshots++;
        stats_.damageDealt += dmg;
        ++streak_;
        bestStreak_ = std::max(bestStreak_, streak_);
        lastShot_.hit = true;
        lastShot_.head = h.head;
        lastShot_.points = static_cast<long long>(dmg);
        const Vector3 p = {ray.position.x + ray.direction.x * h.distance, ray.position.y + ray.direction.y * h.distance,
                           ray.position.z + ray.direction.z * h.distance};
        if (ctx_.fx) ctx_.fx->Burst(p, h.head ? Color{255, 236, 190, 255} : ctx_.targetColor, h.head ? 18 : 8, 3.5f, *ctx_.rng);
        ctx_.audio->Play(h.head ? Sfx::Headshot : Sfx::Hit);
        if (!targets_.empty()) targets_[0].hitFlash = 0.08;
        // The bot notices where it's being shot from.
        botKnows_ = player_.pos;
        botKnowsTime_ = t;
        if (botHp_ <= 0.0) {
            stats_.kills++;
            ctx_.audio->Play(Sfx::Kill);
            if (botVisibleSince_ >= 0.0) {
                stats_.ttkSumMs += (t - botVisibleSince_) * 1000.0;
                stats_.ttkCount++;
            }
            EndRound(t, 1, h.head ? "You killed the bot with a headshot." : "You killed the bot.");
        }
    }

    // First-person Sheriff, drawn on top of the world (painter's order, far
    // parts first), with a kick when firing and a dip while reloading.
    void DrawViewModel() const {
        if (phase_ == Phase::MatchOver) return;
        const World& w = *ctx_.world;
        const Vector3 eye = ctx_.cam->Eye();
        const Vector3 f = ctx_.cam->Forward();
        const double yr = val::DegToRad(ctx_.cam->Yaw());
        const Vector3 right = {static_cast<float>(std::cos(yr)), 0.0f, static_cast<float>(std::sin(yr))};
        const Vector3 up = Cross(right, f);
        const float kick = static_cast<float>(viewKick_ * viewKick_);
        float dip = 0.0f;
        if (gun_.reloading) {
            const float k = static_cast<float>(1.0 - (gun_.readyAt - lastUpdate_) / kReloadTime);
            dip = std::sin(std::max(0.0f, std::min(1.0f, k)) * 3.14159265f) * 0.12f;
        }
        // Barrel points slightly in towards the crosshair and tilts up with the kick.
        const Vector3 gf = Add3(Add3(f, Scale3(up, 0.35f * kick + 0.04f)), Scale3(right, -0.1f));
        const Vector3 gfN = Scale3(gf, 1.0f / Len(gf));
        const Vector3 gu = Cross(right, gfN);
        const Vector3 base = Add3(Add3(Add3(eye, Scale3(right, 0.19f)), Scale3(up, -0.17f - dip)), Scale3(f, 0.5f - 0.04f * kick));
        auto part = [&](float r, float u, float fw) { return Add3(Add3(Add3(base, Scale3(right, r)), Scale3(gu, u)), Scale3(gfN, fw)); };
        const Color metal = {74, 78, 88, 255};
        const Color steel = {140, 146, 158, 255};
        const Color grip = {118, 84, 56, 255};
        const Color glove = {44, 46, 54, 255};
        const Color sleeve = Shade(Color{52, 62, 82, 255}, ctx_.brightness);

        rlDrawRenderBatchActive();
        rlDisableDepthTest();
        // Barrel (farthest), sight, cylinder, frame, grip, hand, sleeve (nearest).
        w.DrawBoxAxes(part(0.0f, 0.02f, 0.13f), Vector3{0.026f, 0.03f, 0.2f}, right, gu, gfN, metal, Surface::Metal);
        w.DrawBoxAxes(part(0.0f, 0.045f, 0.2f), Vector3{0.008f, 0.012f, 0.012f}, right, gu, gfN, steel, Surface::Metal);
        w.DrawCapsuleLit(part(0.0f, 0.005f, 0.0f), part(0.0f, 0.005f, 0.05f), 0.03f, steel, Surface::Metal);
        w.DrawBoxAxes(part(0.0f, 0.0f, -0.02f), Vector3{0.032f, 0.06f, 0.08f}, right, gu, gfN, metal, Surface::Metal);
        w.DrawBoxAxes(part(0.0f, -0.055f, -0.06f), Vector3{0.034f, 0.09f, 0.045f}, right, gu, gfN, grip, Surface::Cloth);
        w.DrawCapsuleLit(part(0.0f, -0.05f, -0.07f), part(0.01f, -0.07f, -0.1f), 0.03f, glove, Surface::Cloth);
        w.DrawCapsuleLit(part(0.012f, -0.075f, -0.12f), part(0.035f, -0.12f, -0.25f), 0.032f, sleeve, Surface::Cloth);
        rlDrawRenderBatchActive();
        rlEnableDepthTest();
    }

    // ------------------------------------------------------------------- bot
    bool CanSee(Vector3 from, const Target& body) const { return mv::CanSee(*ctx_.world, from, body); }

    // Where to go next: spots towards the player's side, more so later in
    // the round; angle-holding spots are preferred early.
    void PickGoal() {
        const double urgency = phase_ == Phase::Live ? 1.0 - std::max(0.0, roundEnd_ - lastUpdate_) / kRoundTime : 0.0;
        int best = 0;
        double bestScore = -1e9;
        for (size_t i = 0; i < nodes_.size(); ++i) {
            const Vector3& w = nodes_[i].p;
            const double dist = Len(Sub(w, bot_.pos));
            if (dist < 2.0) continue;
            const double towardPlayer = (w.z + kHalfZ) / (2.0 * kHalfZ);  // 0 at the bot's side, 1 at the player's
            if (towardPlayer > 0.8 && urgency < 0.5) continue;           // don't walk into the player's spawn early
            const double score = ctx_.rng->Uniform(0.0, 1.0) + towardPlayer * (0.3 + urgency) - dist * 0.015 +
                                 (nodes_[i].hold && urgency < 0.6 ? 0.35 : 0.0);
            if (score > bestScore) {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
        goal_ = best;
        PathTo(goal_);
    }

    Vector3 PathTarget() const { return path_.empty() ? bot_.pos : nodes_[static_cast<size_t>(path_.front())].p; }

    void UpdateBot(double t, float dt, bool live) {
        lastUpdate_ = t;
        if (botHp_ <= 0.0) {
            bot_.vel = Vector3{0.0f, 0.0f, 0.0f};
            return;
        }
        if (botGun_.reloading && t >= botGun_.readyAt) {
            botGun_.reloading = false;
            botGun_.ammo = kMagazine;
        }
        botGun_.Recover(t, dt);

        const Target me = BodyOf(player_);
        const Vector3 botEye = bot_.Eye();
        const Vector3 aimDir = DirectionFromAngles(botYaw_, botPitch_);
        // Sight: player inside the bot's ~110 degree view and not blocked.
        const Vector3 toMe = Sub(TargetAimPoint(me, false), botEye);
        const float distMe = Len(toMe);
        const double facing = (toMe.x * aimDir.x + toMe.y * aimDir.y + toMe.z * aimDir.z) / std::max(1e-3f, distMe);
        const bool sees = live && hp_ > 0.0 && facing > std::cos(val::DegToRad(55.0)) && CanSee(botEye, me);
        // Hearing: running footsteps within 20 m.
        if (live && player_.onGround && player_.Speed() > kFootstepSpeed && distMe < 20.0f) {
            botKnows_ = player_.pos;
            botKnowsTime_ = t;
        }

        float wx = 0.0f, wz = 0.0f, speed = 0.0f;
        bool wantCrouch = false;
        if (!live) {
            // Freeze time / round over: stand still.
        } else if (sees) {
            lastSeen_ = t;
            botKnows_ = player_.pos;
            botKnowsTime_ = t;
            if (!engaged_) {
                engaged_ = true;
                botState_ = BotState::Engaging;
                reactAt_ = t + skill_.reactionMs / 1000.0 * ctx_.rng->Uniform(0.85, 1.25);
                NewBurst(t);
            }
        } else if (engaged_ && t - lastSeen_ > 0.6) {
            engaged_ = false;
            botState_ = BotState::Moving;
            goal_ = NearestNode(botKnows_, false);
            PathTo(goal_);  // go where it last saw you
        }

        if (live && botState_ == BotState::Engaging) {
            // Aim at the player's head or chest plus this burst's error.
            const Vector3 goal = TargetAimPoint(me, aimHead_);
            double gy = 0.0, gp = 0.0;
            AnglesFromDirection(Sub(goal, botEye), gy, gp);
            gy += errYaw_;
            gp += errPitch_;
            if (t >= reactAt_) {
                // Micro-correction: the aim error shrinks while it stays on you.
                const double decay = std::exp(-skill_.correctRate * static_cast<double>(dt));
                errYaw_ *= decay;
                errPitch_ *= decay;
                Turn(gy, gp, skill_.turnDegPerSec * static_cast<double>(dt));
                const double off = std::hypot(val::NormalizeDeg(gy - botYaw_), gp - botPitch_);
                const bool stopper = stopThisBurst_;
                if (t < pauseUntil_) {
                    // Between bursts: ADAD strafe (better bots), otherwise settle.
                    if (strafeThisPause_) {
                        const double yr = val::DegToRad(botYaw_);
                        wx = static_cast<float>(std::cos(yr)) * static_cast<float>(strafeDir_);
                        wz = static_cast<float>(std::sin(yr)) * static_cast<float>(strafeDir_);
                        speed = kRunSpeed;
                        if (t >= flipAt_) {
                            strafeDir_ = -strafeDir_;
                            flipAt_ = t + ctx_.rng->Uniform(0.18, 0.4);
                        }
                    } else if (stopper) {
                        CounterStrafe(bot_, wx, wz, speed);
                    }
                } else {
                    // Counter-strafe before shooting; worse bots keep running
                    // and shoot on the move.
                    if (stopper) CounterStrafe(bot_, wx, wz, speed);
                    else FollowPath(wx, wz, speed, dt);
                    const bool stillEnough = !stopper || bot_.Accurate();
                    if (off < 1.2 + skill_.aimErrDeg && stillEnough && t >= botNextShot_ && !botGun_.reloading && sees) {
                        BotFire(t);
                    }
                }
                wantCrouch = crouchThisBurst_ && t >= pauseUntil_;
            }
        } else if (live) {
            // Moving between spots / holding an angle.
            if (botState_ == BotState::Moving) {
                if (path_.empty()) {
                    botState_ = BotState::Holding;
                    holdUntil_ = t + ctx_.rng->Uniform(1.2, 3.5);
                } else {
                    FollowPath(wx, wz, speed, dt);
                    // Higher ranks walk (silently) once they know where you are.
                    const bool careful = skill_.stopChance > 0.6 && t - botKnowsTime_ < 3.0;
                    if (careful && speed > 0.0f) speed = kWalkSpeed;
                }
            } else if (t >= holdUntil_) {
                botState_ = BotState::Moving;
                PickGoal();
            }
            // No contact for a while: go hunting towards the player's area.
            if (t - roundStart_ > 10.0 && t - botKnowsTime_ > 6.0 && t >= nextHunt_) {
                goal_ = NearestNode(player_.pos, false);
                PathTo(goal_);
                botState_ = BotState::Moving;
                nextHunt_ = t + 5.0;
            }
            // Look where the player probably is, or along the path, or (holding)
            // towards the player's side.
            Vector3 lookAt;
            if (t - botKnowsTime_ < 4.0) {
                lookAt = Vector3{botKnows_.x, 1.4f, botKnows_.z};
            } else if (botState_ == BotState::Moving && !path_.empty()) {
                const Vector3 pt = PathTarget();
                lookAt = Vector3{pt.x, 1.5f, pt.z};
            } else {
                lookAt = Vector3{bot_.pos.x * 0.5f, 1.5f, bot_.pos.z + 12.0f};
            }
            double ly = 0.0, lp = 0.0;
            AnglesFromDirection(Sub(lookAt, botEye), ly, lp);
            Turn(ly, lp, 260.0 * static_cast<double>(dt));
        }

        Step(bot_, wx, wz, speed, wantCrouch, dt, ctx_.world->Covers());
        botWalk_ += bot_.Speed() * dt * 2.6f;
    }

    void FollowPath(float& wx, float& wz, float& speed, float dt) {
        while (!path_.empty()) {
            Vector3 to = Sub(PathTarget(), bot_.pos);
            to.y = 0.0f;
            const float d = Len(to);
            if (d < 0.6f) {
                path_.erase(path_.begin());
                continue;
            }
            wx = to.x / d;
            wz = to.z / d;
            speed = kRunSpeed;
            stuckTime_ = bot_.Speed() < 0.5f ? stuckTime_ + static_cast<double>(dt) : 0.0;
            if (stuckTime_ > 0.8) PathTo(goal_);
            return;
        }
    }

    void Turn(double goalYaw, double goalPitch, double maxStep) {
        const double dy = val::NormalizeDeg(goalYaw - botYaw_);
        const double dp = goalPitch - botPitch_;
        const double len = std::hypot(dy, dp);
        if (len <= maxStep || len < 1e-9) {
            botYaw_ = goalYaw;
            botPitch_ = goalPitch;
        } else {
            botYaw_ = val::NormalizeDeg(botYaw_ + dy / len * maxStep);
            botPitch_ += dp / len * maxStep;
        }
    }

    void NewBurst(double t) {
        burstLeft_ = ctx_.rng->Int(skill_.tapsMin, skill_.tapsMax);
        aimHead_ = ctx_.rng->Chance(skill_.headChance);
        errYaw_ = RandNormal(*ctx_.rng) * skill_.aimErrDeg;
        errPitch_ = RandNormal(*ctx_.rng) * skill_.aimErrDeg * 0.6;
        stopThisBurst_ = ctx_.rng->Chance(skill_.stopChance);
        crouchThisBurst_ = ctx_.rng->Chance(skill_.crouchChance);
        strafeThisPause_ = ctx_.rng->Chance(skill_.strafeChance);
        strafeDir_ = ctx_.rng->Sign();
        flipAt_ = t + ctx_.rng->Uniform(0.18, 0.4);
    }

    void BotFire(double t) {
        --botGun_.ammo;
        --burstLeft_;
        botNextShot_ = t + std::max(kFireInterval, ctx_.rng->Uniform(skill_.tapGapMin, skill_.tapGapMax));
        botGun_.lastShot = t;
        ctx_.audio->Play(Sfx::EnemyShot);

        const double spread = ShotSpread(bot_, botGun_.bloom);
        botGun_.bloom = std::min(kBloomMax, botGun_.bloom + kBloomPerShot);
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double r = spread * std::sqrt(ctx_.rng->Uniform(0.0, 1.0));
        Ray ray;
        ray.position = bot_.Eye();
        ray.direction = DirectionFromAngles(botYaw_ + std::cos(a) * r, botPitch_ + std::sin(a) * r);
        const Target me = BodyOf(player_);
        const TargetHit h = RaycastTarget(me, ray);
        if (h.hit && h.distance < ctx_.world->RaycastCovers(ray)) {
            const double dmg = Damage(h);
            hp_ -= dmg;
            stats_.damageTaken += dmg;
            lastHurtTime_ = t;
            hurtFrom_ = bot_.Eye();
            ctx_.audio->Play(Sfx::Hurt);
            if (hp_ <= 0.0) {
                stats_.deaths++;
                EndRound(t, -1, TextFormat("Killed by the %s bot%s.", TierName(ctx_.botTier), h.head ? " (headshot)" : ""));
            }
        }
        if (botGun_.ammo <= 0) StartReload(botGun_, t);
        if (burstLeft_ <= 0) {
            // Short pause (reposition / let the spread reset), then a new burst.
            pauseUntil_ = t + ctx_.rng->Uniform(skill_.pauseMin, skill_.pauseMax);
            NewBurst(t);
        }
    }

    BotSkill skill_;
    Phase phase_ = Phase::Freeze;
    double phaseEnd_ = 0.0;
    double roundEnd_ = 0.0;
    int round_ = 0;
    int lastRoundResult_ = 0;
    std::string roundNote_;

    // Player.
    Mover player_;
    double hp_ = kMaxHp;
    Gun gun_;
    double viewKick_ = 0.0;
    double lastHurtTime_ = -10.0;
    Vector3 hurtFrom_ = {0.0f, 0.0f, 0.0f};
    double botVisibleSince_ = -1.0;
    bool reactionRecorded_ = false;

    // Bot.
    Mover bot_;
    double botHp_ = kMaxHp;
    Gun botGun_;
    BotState botState_ = BotState::Moving;
    std::vector<NavNode> nodes_;
    std::vector<int> path_;
    int goal_ = 0;
    double holdUntil_ = 0.0;
    double stuckTime_ = 0.0;
    double botYaw_ = 180.0;
    double botPitch_ = 0.0;
    float botWalk_ = 0.0f;
    bool engaged_ = false;
    double reactAt_ = 0.0;
    double lastSeen_ = -10.0;
    Vector3 botKnows_ = {0.0f, 0.0f, 0.0f};
    double botKnowsTime_ = -100.0;
    int burstLeft_ = 0;
    bool aimHead_ = false;
    double errYaw_ = 0.0;
    double errPitch_ = 0.0;
    bool stopThisBurst_ = false;
    bool crouchThisBurst_ = false;
    bool strafeThisPause_ = false;
    int strafeDir_ = 1;
    double flipAt_ = 0.0;
    double pauseUntil_ = 0.0;
    double botNextShot_ = 0.0;
    double lastUpdate_ = 0.0;
    double roundStart_ = 0.0;
    double nextHunt_ = 0.0;
};

}  // namespace

std::unique_ptr<Mode> CreateVsBotMode(const GameContext& ctx) { return std::make_unique<VsBotMode>(ctx); }
