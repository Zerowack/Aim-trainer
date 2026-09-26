// vsbot.cpp - VS Bot: a 1v1 duel in a boxed arena with Valorant-style
// movement (run / walk / crouch / jump, counter-strafing), a Vandal-like
// rifle and a bot whose skill scales from Iron to Radiant.
//
// All movement and weapon numbers below are approximations of how
// Valorant feels; they are grouped here so they are easy to tune.
#include <algorithm>
#include <cmath>
#include <string>

#include "modes.h"
#include "movement.h"
#include "rank.h"
#include "ui.h"

namespace {

using namespace mv;

// ---- Rifle (Vandal-like) ----------------------------------------------------
constexpr double kFireInterval = 1.0 / 9.75;
constexpr int kMagazine = 25;
constexpr double kReloadTime = 2.5;
constexpr double kHeadDamage = 160.0;  // one-tap
constexpr double kBodyDamage = 40.0;
constexpr double kLegDamage = 34.0;
constexpr double kMaxHp = 150.0;  // 100 health + 50 heavy shields
constexpr double kBaseSpread = 0.25;
constexpr double kRunSpread = 5.0;   // extra spread at full running speed
constexpr double kAirSpread = 8.0;
constexpr double kSprayReset = 0.35;  // seconds without firing to recover recoil

// ---- Match -------------------------------------------------------------------
constexpr int kWinRounds = 5;
constexpr double kRoundTime = 60.0;
constexpr double kFreezeTime = 2.5;
constexpr double kEndTime = 2.5;

struct BotSkill {
    double reactionMs;
    double turnDegPerSec;
    double aimErrDeg;     // aim error (std-dev-ish) when it fires
    double headChance;
    double stopChance;    // counter-strafes before shooting
    int burstMin, burstMax;
    double strafeChance;  // ADAD between bursts
    double crouchChance;  // crouches while shooting
    double correctRate;   // how fast it corrects its aim error onto you (1/s)
    double recoilControl; // 0 = ignores recoil, 1 = pulls down perfectly
    double pauseMin, pauseMax;  // seconds between bursts
};

BotSkill SkillFor(int tier) {
    static const BotSkill table[kTierCount] = {
        //  react  turn  err   head  stop  burst   strafe crouch correct recoil pause
        {540.0, 100.0, 4.00, 0.06, 0.10, 5, 10, 0.05, 0.05, 0.3, 0.00, 0.55, 1.00},  // Iron
        {450.0, 150.0, 2.80, 0.14, 0.25, 4, 9, 0.10, 0.08, 0.7, 0.15, 0.45, 0.85},   // Bronze
        {380.0, 210.0, 2.00, 0.24, 0.40, 4, 8, 0.20, 0.10, 1.1, 0.30, 0.35, 0.70},   // Silver
        {320.0, 290.0, 1.40, 0.35, 0.55, 3, 6, 0.30, 0.12, 1.8, 0.45, 0.28, 0.55},   // Gold
        {280.0, 370.0, 1.00, 0.45, 0.70, 2, 5, 0.40, 0.15, 2.5, 0.60, 0.22, 0.48},   // Platinum
        {245.0, 450.0, 0.75, 0.55, 0.80, 2, 4, 0.50, 0.18, 3.3, 0.70, 0.18, 0.42},   // Diamond
        {215.0, 550.0, 0.55, 0.68, 0.90, 1, 3, 0.60, 0.20, 4.2, 0.80, 0.14, 0.36},   // Ascendant
        {190.0, 670.0, 0.40, 0.80, 0.95, 1, 3, 0.70, 0.22, 5.2, 0.88, 0.11, 0.30},   // Immortal
        {165.0, 820.0, 0.26, 0.90, 1.00, 1, 2, 0.80, 0.25, 6.5, 0.95, 0.08, 0.25},   // Radiant
    };
    return table[std::max(0, std::min(kTierCount - 1, tier))];
}

// First-shot spread (degrees) from movement state.
double MoveSpread(const Mover& m) {
    if (!m.onGround) return kAirSpread;
    const double sp = m.Speed();
    double s = kBaseSpread;
    if (sp > kAccurateSpeed) s += kRunSpread * std::min(1.0, (sp - kAccurateSpeed) / (kRunSpeed - kAccurateSpeed));
    if (m.crouch > 0.9f && sp <= kAccurateSpeed) s *= 0.85;  // crouching and still: a little tighter
    return s;
}

class VsBotMode : public Mode {
public:
    explicit VsBotMode(const GameContext& ctx) : Mode(ModeId::VsBot, ctx), skill_(SkillFor(ctx.botTier)) {
        stats_.botTier = ctx.botTier;
        useOnsetReaction_ = false;
        // Build the arena right away so the pre-match countdown already shows
        // it from the spawn (not from inside the middle box).
        BuildArena();
        ctx_.cam->SetEye(Vector3{0.0f, kStandEye, 18.0f});
    }
    ~VsBotMode() override { ctx_.world->ClearCovers(); }
    VsBotMode(const VsBotMode&) = delete;
    VsBotMode& operator=(const VsBotMode&) = delete;

    void Begin(double t) override {
        StartRound(t);
    }

    bool Finished() const override { return phase_ == Phase::MatchOver; }

    bool HudTimer(double t, double& secondsLeft, std::string& label) const override {
        secondsLeft = phase_ == Phase::Live ? std::max(0.0, roundEnd_ - t) : kRoundTime;
        label = TextFormat("YOU %d  :  %d BOT (%s)", stats_.roundsWon, stats_.roundsLost, TierName(ctx_.botTier));
        return true;
    }

    void OnShot(double t) override { Fire(t); }

    void Update(double t, double dt, bool triggerHeld) override {
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
        UpdatePlayer(t, fdt, live, triggerHeld);
        UpdateBot(t, fdt, live);

        // Keep the bot's drawable/hittable body in sync.
        if (!targets_.empty()) {
            targets_[0].pos = bot_.pos;
            targets_[0].crouch = bot_.crouch;
        }
        stats_.score = std::max<long long>(0, stats_.roundsWon * 1000 - stats_.roundsLost * 300 +
                                                  static_cast<long long>(std::lround(stats_.damageDealt)));
    }

    void Draw3D() const override {
        ctx_.world->DrawCovers();
        if (botHp_ > 0.0) Mode::Draw3D();
    }

    void DrawHud(double t) const override {
        const float vw = ui::VW(), vh = ui::VH();
        const float cx = vw * 0.5f, cy = vh * 0.5f;

        // Damage flash.
        const double hurt = t - lastHurtTime_;
        if (hurt >= 0.0 && hurt < 0.35) {
            const float a = static_cast<float>(0.35 * (1.0 - hurt / 0.35));
            ui::Fill(Rectangle{0.0f, 0.0f, vw, vh}, ui::Alpha(Color{200, 20, 30, 255}, a));
        }

        // Movement accuracy hint under the crosshair.
        if (phase_ == Phase::Live) {
            const bool accurate = player_.onGround && player_.Speed() <= kAccurateSpeed;
            ui::Text(accurate ? "ACCURATE" : (player_.onGround ? "MOVING" : "AIRBORNE"), cx, cy + 34.0f, 14.0f,
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
        ui::TextBold(TextFormat("%d", ammo_), am.x + 18.0f, am.y + 10.0f, 34.0f, ammo_ > 5 ? ui::theme::kText : ui::theme::kAccent);
        ui::Text(TextFormat("/ %d   RIFLE", kMagazine), am.x + 80.0f, am.y + 22.0f, 18.0f, ui::theme::kTextDim);
        if (reloading_) {
            const float k = static_cast<float>(std::min(1.0, std::max(0.0, 1.0 - (readyAt_ - t) / kReloadTime)));
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
            ui::Text(TextFormat("Starts in %d  -  WASD move, Shift walk, Ctrl crouch, Space jump", std::max(1, n)), cx, cy - 104.0f,
                     20.0f, ui::theme::kTextDim, ui::Align::Center);
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

    void BuildArena() {
        std::vector<Box> b;
        // Outer walls (arena 28 x 40 m).
        b.push_back(Box{Vector3{-14.25f, 1.75f, 0.0f}, Vector3{0.5f, 3.5f, 41.0f}});
        b.push_back(Box{Vector3{14.25f, 1.75f, 0.0f}, Vector3{0.5f, 3.5f, 41.0f}});
        b.push_back(Box{Vector3{0.0f, 1.75f, -20.25f}, Vector3{29.0f, 3.5f, 0.5f}});
        b.push_back(Box{Vector3{0.0f, 1.75f, 20.25f}, Vector3{29.0f, 3.5f, 0.5f}});
        // Tall boxes (full cover) and low boxes (crouch cover, head stays visible).
        auto add = [&](float x, float z, float sx, float sz, float h) { b.push_back(Box{Vector3{x, h * 0.5f, z}, Vector3{sx, h, sz}}); };
        add(0.0f, 0.0f, 2.4f, 2.4f, 2.6f);
        add(-6.0f, -9.0f, 2.2f, 2.2f, 2.6f);
        add(6.0f, 9.0f, 2.2f, 2.2f, 2.6f);
        add(6.0f, -9.0f, 3.0f, 1.0f, 1.05f);
        add(-6.0f, 9.0f, 3.0f, 1.0f, 1.05f);
        add(-10.0f, 0.0f, 1.2f, 4.0f, 2.6f);
        add(10.0f, 0.0f, 1.2f, 4.0f, 2.6f);
        add(0.0f, -14.5f, 4.0f, 1.0f, 1.05f);
        add(0.0f, 14.5f, 4.0f, 1.0f, 1.05f);
        add(-3.8f, -4.6f, 1.3f, 1.3f, 1.55f);
        add(3.8f, 4.6f, 1.3f, 1.3f, 1.55f);
        ctx_.world->SetCovers(b);

        // Spots the bot moves between (all in the open).
        waypoints_ = {{-10.5f, 0.0f, -16.0f}, {10.5f, 0.0f, -16.0f}, {0.0f, 0.0f, -11.0f}, {-6.5f, 0.0f, -4.5f}, {6.5f, 0.0f, -4.0f},
                      {-11.8f, 0.0f, 4.5f},   {11.8f, 0.0f, 4.5f},   {-2.5f, 0.0f, 6.5f},  {2.5f, 0.0f, -6.5f}, {0.0f, 0.0f, 11.0f},
                      {-9.0f, 0.0f, 15.0f},   {9.0f, 0.0f, 15.0f},   {-12.0f, 0.0f, -8.0f}, {12.0f, 0.0f, 8.0f}};
    }

    void StartRound(double t) {
        ++round_;
        phase_ = Phase::Freeze;
        phaseEnd_ = t + kFreezeTime;
        roundStart_ = t + kFreezeTime;
        nextHunt_ = 0.0;
        player_ = Mover{};
        player_.pos = Vector3{ctx_.rng->UniformF(-3.0f, 3.0f), 0.0f, 18.0f};
        bot_ = Mover{};
        bot_.pos = Vector3{ctx_.rng->UniformF(-3.0f, 3.0f), 0.0f, -18.0f};
        hp_ = kMaxHp;
        botHp_ = kMaxHp;
        ammo_ = kMagazine;
        reloading_ = false;
        readyAt_ = t;
        sprayIndex_ = 0;
        botAmmo_ = kMagazine;
        botReloadEnd_ = 0.0;
        botState_ = BotState::Moving;
        PickWaypoint();
        botYaw_ = 180.0;  // facing the player's side
        botPitch_ = 0.0;
        engaged_ = false;
        botVisibleSince_ = -1.0;
        reactionRecorded_ = false;
        ctx_.cam->Reset(0.0, 0.0);
        ctx_.cam->SetEye(player_.Eye());
        ctx_.history->Clear();
        targets_.clear();
        Target bt = BodyOf(bot_);
        targets_.push_back(bt);
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
    void UpdatePlayer(double t, float dt, bool live, bool triggerHeld) {
        StepPlayer(player_, ctx_.cam->Yaw(), live, 1.0f, dt, ctx_.world->Covers());
        if (live && IsKeyPressed(KEY_R) && !reloading_ && ammo_ < kMagazine) StartReload(t);
        ctx_.cam->SetEye(player_.Eye());

        if (reloading_ && t >= readyAt_) {
            reloading_ = false;
            ammo_ = kMagazine;
        }
        if (t - lastShotTime_ > kSprayReset) sprayIndex_ = 0;
        // Full auto while the trigger is held.
        if (live && triggerHeld && t >= readyAt_ && !reloading_) Fire(t);

        // When did the bot become visible to the player (reaction / TTK)?
        if (live && botHp_ > 0.0) {
            const Target body = BodyOf(bot_);
            const bool onScreen = AngleToPoint(*ctx_.cam, TargetAimPoint(body, false)) < 50.0;
            if (onScreen && CanSee(player_.Eye(), body)) {
                if (botVisibleSince_ < 0.0) botVisibleSince_ = t;
            } else {
                botVisibleSince_ = -1.0;
                reactionRecorded_ = false;
            }
        }
    }

    void StartReload(double t) {
        reloading_ = true;
        readyAt_ = t + kReloadTime;
    }

    void Fire(double t) {
        if (phase_ != Phase::Live || reloading_ || t < readyAt_) return;
        if (ammo_ <= 0) {
            StartReload(t);
            return;
        }
        if (t - lastShotTime_ > kSprayReset) sprayIndex_ = 0;

        // Spread = movement + bloom; recoil climbs upwards then sways sideways.
        const bool moving = !player_.onGround || player_.Speed() > kAccurateSpeed;
        const double spread = MoveSpread(player_) + std::min(2.5, sprayIndex_ * 0.3);
        const double kickUp = std::min(sprayIndex_, 8) * 0.42;
        const double kickSide = sprayIndex_ > 8 ? std::sin(sprayIndex_ * 0.9) * 1.4 : 0.0;
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double r = spread * std::sqrt(ctx_.rng->Uniform(0.0, 1.0));
        Ray ray;
        ray.position = player_.Eye();
        ray.direction = DirectionFromAngles(ctx_.cam->Yaw() + kickSide + std::cos(a) * r, ctx_.cam->Pitch() + kickUp + std::sin(a) * r);

        stats_.shots++;
        if (moving) stats_.movingShots++;
        ctx_.audio->Play(Sfx::Shot);
        --ammo_;
        ++sprayIndex_;
        lastShotTime_ = t;
        readyAt_ = t + kFireInterval;
        if (ammo_ <= 0) StartReload(t);

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
        const double dmg = h.head ? kHeadDamage : (h.legs ? kLegDamage : kBodyDamage);
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

    // ------------------------------------------------------------------- bot
    bool CanSee(Vector3 from, const Target& body) const { return mv::CanSee(*ctx_.world, from, body); }

    void PickWaypoint() {
        // Later in the round the bot pushes towards the player's side.
        const double urgency = phase_ == Phase::Live ? 1.0 - std::max(0.0, roundEnd_ - lastUpdate_) / kRoundTime : 0.0;
        int best = 0;
        double bestScore = -1e9;
        for (size_t i = 0; i < waypoints_.size(); ++i) {
            const Vector3& w = waypoints_[i];
            const double dist = Len(Sub(w, bot_.pos));
            if (dist < 2.0) continue;
            const double towardPlayer = (w.z + 20.0) / 40.0;  // 0 at bot side, 1 at player side
            const double score = ctx_.rng->Uniform(0.0, 1.0) + towardPlayer * (0.3 + urgency) - dist * 0.02;
            if (score > bestScore) {
                bestScore = score;
                best = static_cast<int>(i);
            }
        }
        waypoint_ = waypoints_[static_cast<size_t>(best)];
        stuckTime_ = 0.0;
    }

    void UpdateBot(double t, float dt, bool live) {
        lastUpdate_ = t;
        if (botHp_ <= 0.0) {
            bot_.vel = Vector3{0.0f, 0.0f, 0.0f};
            return;
        }
        if (botReloadEnd_ > 0.0 && t >= botReloadEnd_) {
            botReloadEnd_ = 0.0;
            botAmmo_ = kMagazine;
        }

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
            waypoint_ = botKnows_;  // go where it last saw you
            stuckTime_ = 0.0;
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
                const double decay = std::exp(-skill_.correctRate * dt);
                errYaw_ *= decay;
                errPitch_ *= decay;
                Turn(gy, gp, skill_.turnDegPerSec * dt);
                const double off = std::hypot(val::NormalizeDeg(gy - botYaw_), gp - botPitch_);
                const bool stopper = stopThisBurst_;
                if (t < pauseUntil_) {
                    // Between bursts: ADAD strafe (better bots), otherwise settle.
                    if (strafeThisPause_) {
                        const double yr = val::DegToRad(botYaw_);
                        wx = static_cast<float>(std::cos(yr)) * static_cast<float>(strafeDir_);
                        wz = static_cast<float>(std::sin(yr)) * static_cast<float>(strafeDir_);
                        speed = kRunSpeed;
                    } else if (stopper) {
                        CounterStrafe(wx, wz, speed);
                    }
                } else {
                    // Counter-strafe (push against the motion) before shooting;
                    // worse bots keep running and spray on the move.
                    if (stopper) {
                        CounterStrafe(wx, wz, speed);
                    } else {
                        Vector3 to = Sub(waypoint_, bot_.pos);
                        to.y = 0.0f;
                        const float d = Len(to);
                        if (d > 0.6f) {
                            wx = to.x / d;
                            wz = to.z / d;
                            speed = kRunSpeed;
                        } else {
                            PickWaypoint();
                        }
                    }
                    const bool stillEnough = !stopper || bot_.Speed() <= kAccurateSpeed;
                    if (off < 1.2 + skill_.aimErrDeg && stillEnough && t >= botNextShot_ && botReloadEnd_ <= 0.0 && sees) {
                        BotFire(t);
                    }
                }
                wantCrouch = crouchThisBurst_ && t >= pauseUntil_;
            }
        } else if (live) {
            // Moving between spots / holding an angle.
            Vector3 to = Sub(waypoint_, bot_.pos);
            to.y = 0.0f;
            const float d = Len(to);
            if (botState_ == BotState::Moving) {
                if (d < 0.6f) {
                    botState_ = BotState::Holding;
                    holdUntil_ = t + ctx_.rng->Uniform(1.0, 3.0);
                } else {
                    wx = to.x / d;
                    wz = to.z / d;
                    // Higher ranks walk (silently) once they know where you are.
                    const bool careful = skill_.stopChance > 0.6 && t - botKnowsTime_ < 3.0;
                    speed = careful ? kWalkSpeed : kRunSpeed;
                    stuckTime_ = bot_.Speed() < 0.5f ? stuckTime_ + dt : 0.0;
                    if (stuckTime_ > 0.8) PickWaypoint();
                }
            } else if (t >= holdUntil_) {
                botState_ = BotState::Moving;
                PickWaypoint();
            }
            // No contact for a while: go hunting towards the player's area
            // (like a real player rotating to where the enemy must be).
            if (botState_ != BotState::Engaging && t - roundStart_ > 10.0 && t - botKnowsTime_ > 6.0 && t >= nextHunt_) {
                size_t best = 0;
                float bestD = 1e9f;
                for (size_t i = 0; i < waypoints_.size(); ++i) {
                    const float dd = Len(Sub(waypoints_[i], player_.pos)) + ctx_.rng->UniformF(0.0f, 6.0f);
                    if (dd < bestD) {
                        bestD = dd;
                        best = i;
                    }
                }
                waypoint_ = waypoints_[best];
                botState_ = BotState::Moving;
                stuckTime_ = 0.0;
                nextHunt_ = t + 4.0;
            }
            // Look where the player probably is (or along the path).
            Vector3 lookAt = t - botKnowsTime_ < 4.0 ? Vector3{botKnows_.x, 1.4f, botKnows_.z}
                                                      : Vector3{bot_.pos.x + wx * 5.0f, 1.5f, bot_.pos.z + wz * 5.0f + (d < 0.6f ? 5.0f : 0.0f)};
            double ly = 0.0, lp = 0.0;
            AnglesFromDirection(Sub(lookAt, botEye), ly, lp);
            Turn(ly, lp, 240.0 * dt);
        }

        Step(bot_, wx, wz, speed, wantCrouch, dt, ctx_.world->Covers());
    }

    void CounterStrafe(float& wx, float& wz, float& speed) const { mv::CounterStrafe(bot_, wx, wz, speed); }

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
        burstLeft_ = ctx_.rng->Int(skill_.burstMin, skill_.burstMax);
        aimHead_ = ctx_.rng->Chance(skill_.headChance);
        errYaw_ = RandNormal(*ctx_.rng) * skill_.aimErrDeg;
        errPitch_ = RandNormal(*ctx_.rng) * skill_.aimErrDeg * 0.6;
        stopThisBurst_ = ctx_.rng->Chance(skill_.stopChance);
        crouchThisBurst_ = ctx_.rng->Chance(skill_.crouchChance);
        strafeThisPause_ = ctx_.rng->Chance(skill_.strafeChance);
        strafeDir_ = ctx_.rng->Sign();
        (void)t;
    }

    void BotFire(double t) {
        --botAmmo_;
        --burstLeft_;
        botNextShot_ = t + kFireInterval;
        if (t - botLastShot_ > kSprayReset) botSpray_ = 0;
        botLastShot_ = t;
        ctx_.audio->Play(Sfx::EnemyShot);

        const double spread = MoveSpread(bot_) + std::min(2.5, botSpray_ * 0.3);
        ++botSpray_;
        const double a = ctx_.rng->Uniform(0.0, 2.0 * val::kPi);
        const double r = spread * std::sqrt(ctx_.rng->Uniform(0.0, 1.0));
        Ray ray;
        ray.position = bot_.Eye();
        const double kick = std::min(botSpray_, 8) * 0.42 * (1.0 - skill_.recoilControl);
        ray.direction = DirectionFromAngles(botYaw_ + std::cos(a) * r, botPitch_ + kick + std::sin(a) * r);
        const Target me = BodyOf(player_);
        const TargetHit h = RaycastTarget(me, ray);
        if (h.hit && h.distance < ctx_.world->RaycastCovers(ray)) {
            const double dmg = h.head ? kHeadDamage : (h.legs ? kLegDamage : kBodyDamage);
            hp_ -= dmg;
            stats_.damageTaken += dmg;
            lastHurtTime_ = t;
            ctx_.audio->Play(Sfx::Hurt);
            if (hp_ <= 0.0) {
                stats_.deaths++;
                EndRound(t, -1, TextFormat("Killed by the %s bot%s.", TierName(ctx_.botTier), h.head ? " (headshot)" : ""));
            }
        }
        if (botAmmo_ <= 0) botReloadEnd_ = t + kReloadTime;
        if (burstLeft_ <= 0) {
            // Short pause (tap/burst discipline), then a fresh burst with new error.
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

    Mover player_;
    double hp_ = kMaxHp;
    int ammo_ = kMagazine;
    bool reloading_ = false;
    double readyAt_ = 0.0;
    int sprayIndex_ = 0;
    double lastShotTime_ = -10.0;
    double lastHurtTime_ = -10.0;
    double botVisibleSince_ = -1.0;
    bool reactionRecorded_ = false;

    Mover bot_;
    double botHp_ = kMaxHp;
    BotState botState_ = BotState::Moving;
    std::vector<Vector3> waypoints_;
    Vector3 waypoint_ = {0.0f, 0.0f, 0.0f};
    double holdUntil_ = 0.0;
    double stuckTime_ = 0.0;
    double botYaw_ = 180.0;
    double botPitch_ = 0.0;
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
    double pauseUntil_ = 0.0;
    double botNextShot_ = 0.0;
    double botLastShot_ = -10.0;
    int botSpray_ = 0;
    int botAmmo_ = kMagazine;
    double botReloadEnd_ = 0.0;
    double lastUpdate_ = 0.0;
    double roundStart_ = 0.0;
    double nextHunt_ = 0.0;
};

}  // namespace

std::unique_ptr<Mode> CreateVsBotMode(const GameContext& ctx) { return std::make_unique<VsBotMode>(ctx); }
