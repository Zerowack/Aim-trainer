// rank.cpp - aim rank estimate from run statistics.
#include "rank.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Start value of each tier (Iron, Bronze, Silver, Gold, Platinum, Diamond,
// Ascendant, Immortal, Radiant) for each ranked mode.
//
// Calibration (v2.3.1): a simulated player with the motor skill of a typical
// player at the start of each Valorant tier (reaction time, Fitts' law flick
// speed, endpoint scatter, visual-motor delay while tracking, hand drift)
// played every mode in the real game code, and the values it reached became
// the cut-offs. So one skill level gives the same rank in every mode, and a
// rank here means that tier's aim, not just a good score. Tracking a strafing
// target is capped by human reaction to direction changes, so its numbers
// are low. Strafe Tap, Target Switch and Peek depend on strategy the model
// only partly captures; their cut-offs are smoothed from its kill rates.
struct Thresholds {
    double start[kTierCount];
    bool lowerIsBetter;
};

bool ThresholdsFor(ModeId m, Thresholds& t) {
    switch (m) {
        // Gridshot: kills per second x accuracy.
        case ModeId::Gridshot: t = {{1.65, 1.95, 2.25, 2.55, 2.85, 3.20, 3.55, 3.95, 4.65}, false}; return true;
        // Microshot: head-size kills per second x accuracy.
        case ModeId::Microshot: t = {{0.59, 0.65, 0.73, 0.80, 0.88, 1.05, 1.22, 1.48, 1.95}, false}; return true;
        // Tracking: time on target (percent) x firing discipline.
        case ModeId::Tracking: t = {{10.0, 14.0, 18.0, 22.0, 25.0, 28.0, 31.0, 34.0, 37.0}, false}; return true;
        // Flick 180: kills per second x accuracy.
        case ModeId::Flick180: t = {{0.52, 0.63, 0.72, 0.81, 0.94, 1.06, 1.19, 1.31, 1.50}, false}; return true;
        // Reaction: average ms (includes your monitor/system latency).
        case ModeId::Reaction: t = {{290.0, 272.0, 260.0, 250.0, 241.0, 232.0, 222.0, 212.0, 198.0}, true}; return true;
        // Peek: peeks killed per second x accuracy (the peek rate caps this near 0.58).
        case ModeId::Peek: t = {{0.02, 0.05, 0.08, 0.13, 0.19, 0.26, 0.32, 0.38, 0.45}, false}; return true;
        // Sniper: share of peeks killed (deaths count against you) x accuracy
        // x speed (time to kill after the enemy showed up). Per peek, so every
        // rifle has the same ceiling.
        case ModeId::Sniper: t = {{0.08, 0.15, 0.23, 0.31, 0.40, 0.49, 0.58, 0.67, 0.78}, false}; return true;
        // Classic scenarios: kills per second x accuracy (tracking: time on target).
        case ModeId::Headshot: t = {{0.58, 0.70, 0.82, 0.93, 1.03, 1.15, 1.28, 1.39, 1.57}, false}; return true;
        case ModeId::Sixshot: t = {{1.20, 1.50, 1.80, 2.05, 2.30, 2.75, 3.25, 3.85, 4.55}, false}; return true;
        case ModeId::Spidershot: t = {{0.91, 1.02, 1.16, 1.28, 1.41, 1.60, 1.77, 2.05, 2.45}, false}; return true;
        case ModeId::Motionshot: t = {{1.10, 1.28, 1.45, 1.67, 1.85, 2.13, 2.43, 2.77, 3.40}, false}; return true;
        case ModeId::SmoothTrack: t = {{15.0, 22.0, 30.0, 41.0, 51.0, 62.0, 75.0, 88.0, 97.0}, false}; return true;
        case ModeId::StrafeTap: t = {{0.10, 0.13, 0.17, 0.22, 0.28, 0.36, 0.46, 0.60, 0.80}, false}; return true;
        case ModeId::TargetSwitch: t = {{0.09, 0.12, 0.15, 0.19, 0.23, 0.27, 0.31, 0.36, 0.42}, false}; return true;
        case ModeId::LongRange: t = {{0.63, 0.73, 0.85, 0.96, 1.08, 1.21, 1.34, 1.47, 1.70}, false}; return true;
        case ModeId::Microflex: t = {{1.07, 1.19, 1.29, 1.37, 1.45, 1.54, 1.63, 1.71, 1.82}, false}; return true;
        case ModeId::Popcorn: t = {{0.30, 0.62, 0.95, 1.18, 1.40, 1.75, 2.10, 2.45, 2.80}, false}; return true;
        // Crosshair Placement: average angle from the crosshair to the head at
        // the moment the agent peeks out (deg). The angle is lit beforehand, so
        // this measures how precisely you pre-aim the edge at head height.
        case ModeId::Placement: t = {{7.0, 5.5, 4.4, 3.5, 2.8, 2.2, 1.7, 1.3, 0.9}, true}; return true;
        default: return false;
    }
}

// Converts a skill value to continuous rank points using the thresholds.
double PointsFromValue(double v, const Thresholds& t) {
    // Work in "higher is better" space.
    auto key = [&](double x) { return t.lowerIsBetter ? -x : x; };
    const double x = key(v);
    if (x >= key(t.start[kTierCount - 1])) return kRadiantPoints;
    if (x < key(t.start[0])) {
        // Below Iron's start: spread the Iron divisions over the space below.
        const double span = key(t.start[1]) - key(t.start[0]);
        const double p = 3.0 - (key(t.start[0]) - x) / span * 3.0;
        return std::max(0.0, std::min(2.999, p));
    }
    for (int k = 0; k < kTierCount - 1; ++k) {
        const double lo = key(t.start[k]), hi = key(t.start[k + 1]);
        if (x < hi) return 3.0 * k + 3.0 * (x - lo) / (hi - lo);
    }
    return kRadiantPoints;
}

}  // namespace

const char* TierName(int tier) {
    static const char* const names[kTierCount] = {"Iron",    "Bronze",  "Silver",    "Gold",   "Platinum",
                                                  "Diamond", "Ascendant", "Immortal", "Radiant"};
    return names[std::max(0, std::min(kTierCount - 1, tier))];
}

Color TierColor(int tier) {
    static const Color colors[kTierCount] = {{120, 124, 132, 255}, {176, 116, 64, 255},  {186, 196, 206, 255},
                                             {232, 184, 60, 255},  {64, 186, 180, 255},  {178, 124, 236, 255},
                                             {64, 200, 124, 255},  {224, 58, 78, 255},   {255, 232, 150, 255}};
    return colors[std::max(0, std::min(kTierCount - 1, tier))];
}

std::string RankLabel(const AimRank& r) {
    if (r.tier >= kTierCount - 1) return "Radiant";
    return std::string(TierName(r.tier)) + " " + std::to_string(r.division);
}

const char* RankRoast(const AimRank& r, unsigned int seed) {
    static const char* const iron[] = {
        "Just delete the game, it's not for u lil bro.",
        "Your aim is dogshit. The targets are literally standing still, bro.",
        "Were you aiming with your monitor turned off?",
        "Uninstall. Go outside. Find a new hobby. Maybe knitting.",
        "The bots filed a restraining order because you keep missing them.",
        "Your mouse is begging to be unplugged.",
        "A Roomba with a crosshair would out-frag you.",
    };
    static const char* const bronze[] = {
        "Bronze. You shoot like the mouse is upside down and on fire.",
        "Your crosshair has commitment issues and zero game sense.",
        "Hardstuck and it shows. Every single shot shows.",
        "You miss more than a stormtrooper with his eyes closed.",
        "Your aim is so bad the enemy team feels guilty killing you.",
        "Bronze aim, Radiant excuses.",
    };
    static const char* const silver[] = {
        "Silver: you hit shots... occasionally... mostly by accident.",
        "Your aim is trash with extra steps.",
        "You're the reason your team types 'ff 15'.",
        "Aim of a Jett main who forgot she has a dash and a brain.",
        "Mid is a compliment you haven't earned yet.",
        "Your flicks have the confidence of a first date.",
    };
    static const char* const gold[] = {
        "Gold. Perfectly average. Nobody will ever remember a single shot you hit.",
        "Your aim won't lose you games. Your team still carries you anyway.",
        "Gold aim: good enough to peek, not good enough to win the duel.",
        "You're the human definition of 'mid'. Congrats, I guess.",
        "Stop crouch-spraying across the map. It's embarrassing.",
    };
    static const char* const plat[] = {
        "Platinum. Now stop whiffing the first bullet like it owes you money.",
        "Decent hands, room-temperature crosshair placement.",
        "Your teammates still type 'diff'. They mean you.",
        "Plat aim is a participation trophy with extra steps.",
        "You're dangerous... to your own team's KDA.",
    };
    static const char* const diamond[] = {
        "Diamond. Clean, but Ascendants eat you for breakfast.",
        "Nice flicks. Shame about the brain attached to them.",
        "Good aim, hardstuck anyway. Must be the rest of you.",
        "Diamond hands, Iron decision making.",
        "You're good. Not 'stop playing aim trainers' good. Keep clicking.",
    };
    static const char* const ascendant[] = {
        "Ascendant aim. So why are you still hardstuck, champ?",
        "Cracked mechanics, washed game sense.",
        "Someone is typing 'reported' in all chat. They're not wrong.",
        "Your aim carries. Your utility usage does not.",
        "Almost Immortal. Almost. Like always.",
    };
    static const char* const immortal[] = {
        "Immortal aim. Touch grass, the sun misses you.",
        "Reported for aimbot. Twice. By your own team.",
        "Your mouse deserves a raise, you deserve a shower.",
        "Heads are magnets for your crosshair. Now get a life.",
        "One step from Radiant and a thousand steps from a social life.",
    };
    static const char* const radiant[] = {
        "Radiant. Either you're a pro or your crosshair is glued to heads. Log off.",
        "Touch grass. Please. You've peaked, there's nothing left here.",
        "Absolutely inhuman. The anticheat wants a word.",
        "Go sign a contract or go outside. Pick one.",
        "Your aim is perfect. Your sleep schedule is not.",
    };
    struct List {
        const char* const* lines;
        unsigned int count;
    };
    static const List lists[kTierCount] = {{iron, 7}, {bronze, 6}, {silver, 6}, {gold, 5}, {plat, 5},
                                           {diamond, 5}, {ascendant, 5}, {immortal, 5}, {radiant, 5}};
    const List& l = lists[std::max(0, std::min(kTierCount - 1, r.tier))];
    // Iron 1 gets the classic most of the time.
    if (r.tier == 0 && r.division == 1 && (seed % 3u) != 0u) return iron[0];
    return l.lines[seed % l.count];
}

const char* RankDescription(const AimRank& r) {
    static const char* const desc[kTierCount] = {
        "Getting started. Focus on accuracy first, speed comes later.",
        "Building basics. Slow down and click only when you're on target.",
        "Consistent on easy shots. Work on flick distance control.",
        "Solid, average aim. Tighten up micro-adjustments.",
        "Above average. Your flicks are reliable.",
        "Strong aim with good speed and accuracy.",
        "Excellent aim. Mechanics are rarely your problem.",
        "Top-tier aim. Fast and precise.",
        "Elite aim. Pro-level mechanics.",
    };
    return desc[std::max(0, std::min(kTierCount - 1, r.tier))];
}

AimRank RankFromPoints(double points) {
    AimRank r;
    r.points = std::max(0.0, std::min(kRadiantPoints, points));
    if (r.points >= kRadiantPoints) {
        r.tier = kTierCount - 1;
        r.division = 1;
        r.progress = 1.0;
        return r;
    }
    const int step = static_cast<int>(std::floor(r.points));  // 0..23
    r.tier = step / 3;
    r.division = step % 3 + 1;
    r.progress = r.points - static_cast<double>(step);
    return r;
}

bool RankFromRecord(const RunRecord& rec, AimRank& out) {
    Thresholds t;
    if (!ThresholdsFor(rec.mode, t)) return false;
    if (rec.duration < kMinRankedSeconds) return false;

    double value = 0.0;
    const double acc = std::max(0.0, std::min(1.0, rec.accuracy / 100.0));
    switch (rec.mode) {
        case ModeId::Placement:
            if (rec.placementErr < 0.0 || rec.hits < 5) return false;
            value = rec.placementErr;
            break;
        case ModeId::Tracking:
        case ModeId::SmoothTrack:
            if (rec.trackingPct < 0.0) return false;
            // Time on target, weighed by how much of your firing was on target
            // (holding the trigger the whole time doesn't pay).
            value = rec.trackingPct * (0.6 + 0.4 * acc);
            break;
        case ModeId::Reaction:
            if (rec.avgReactionMs <= 0.0 || rec.hits < 3) return false;
            value = rec.avgReactionMs;
            break;
        case ModeId::Sniper: {
            // Runs from before v1.9 (a different sniper mode) have no peek data.
            if (rec.peeks < 5 || rec.kills < 0) return false;
            const double kills = std::max(0.0, rec.kills - 0.5 * std::max(0, rec.deaths));
            const double ttk = rec.avgTtkMs > 0.0 ? rec.avgTtkMs : 1150.0;
            const double speed = std::max(0.3, std::min(1.0, (1150.0 - ttk) / 800.0));
            value = kills / rec.peeks * acc * speed;
            break;
        }
        default:
            if (rec.hits < 5) return false;
            value = static_cast<double>(rec.hits) / rec.duration * acc;
            break;
    }
    // Difficulty: harder runs are worth more (lower-is-better stats shrink).
    const double mult = GetDifficulty(rec.difficulty).rankMult;
    value = t.lowerIsBetter ? value / mult : value * mult;
    out = RankFromPoints(PointsFromValue(value, t));
    return true;
}

bool ModeRank(const StatsStore& stats, ModeId mode, AimRank& out, int* rankedRuns) {
    std::vector<double> pts;
    const std::vector<const RunRecord*> runs = stats.ForMode(mode);
    int total = 0;
    for (auto it = runs.rbegin(); it != runs.rend(); ++it) {
        AimRank r;
        if (!RankFromRecord(**it, r)) continue;
        ++total;
        if (pts.size() < 5) pts.push_back(r.points);
    }
    if (rankedRuns) *rankedRuns = total;
    if (pts.empty()) return false;
    std::sort(pts.begin(), pts.end());
    const size_t n = pts.size();
    const double median = (n % 2 == 1) ? pts[n / 2] : 0.5 * (pts[n / 2 - 1] + pts[n / 2]);
    out = RankFromPoints(median);
    return true;
}

bool OverallRank(const StatsStore& stats, AimRank& out, int* modesUsed) {
    double sum = 0.0;
    int n = 0;
    for (int i = 0; i < kPlayableModeCount; ++i) {
        AimRank r;
        if (ModeRank(stats, static_cast<ModeId>(i), r)) {
            sum += r.points;
            ++n;
        }
    }
    if (modesUsed) *modesUsed = n;
    if (n < kMinModesForOverall) return false;
    out = RankFromPoints(sum / n);
    return true;
}
