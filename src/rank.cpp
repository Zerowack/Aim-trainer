// rank.cpp - aim rank estimate from run statistics.
#include "rank.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

// Start value of each tier (Iron, Bronze, Silver, Gold, Platinum, Diamond,
// Ascendant, Immortal, Radiant) for each ranked mode. Calibrated estimates
// for this trainer's target sizes and distances.
struct Thresholds {
    double start[kTierCount];
    bool lowerIsBetter;
};

bool ThresholdsFor(ModeId m, Thresholds& t) {
    switch (m) {
        // Gridshot: kills per second x accuracy.
        case ModeId::Gridshot: t = {{2.0, 2.6, 3.2, 3.8, 4.4, 5.0, 5.6, 6.3, 7.2}, false}; return true;
        // Microshot: head-size kills per second x accuracy.
        case ModeId::Microshot: t = {{0.60, 0.80, 1.00, 1.20, 1.40, 1.60, 1.80, 2.00, 2.30}, false}; return true;
        // Tracking: time on target (percent) x firing discipline.
        case ModeId::Tracking: t = {{36.0, 44.0, 51.0, 58.0, 64.0, 70.0, 76.0, 82.0, 88.0}, false}; return true;
        // Flick 180: kills per second x accuracy.
        case ModeId::Flick180: t = {{0.45, 0.60, 0.75, 0.90, 1.05, 1.20, 1.35, 1.50, 1.70}, false}; return true;
        // Reaction: average ms (includes your monitor/system latency).
        case ModeId::Reaction: t = {{300.0, 280.0, 262.0, 248.0, 236.0, 225.0, 215.0, 205.0, 192.0}, true}; return true;
        // Peek: peeks killed per second x accuracy (the peek rate caps this near 0.58).
        case ModeId::Peek: t = {{0.20, 0.26, 0.31, 0.36, 0.40, 0.44, 0.48, 0.51, 0.55}, false}; return true;
        // Sniper: share of peeks killed (deaths count against you) x accuracy
        // x speed (time to kill after the enemy showed up). Per peek, so every
        // rifle has the same ceiling.
        case ModeId::Sniper: t = {{0.08, 0.15, 0.23, 0.31, 0.40, 0.49, 0.58, 0.67, 0.78}, false}; return true;
        // Crosshair Placement: average angle to the head when agents appear (deg).
        case ModeId::Placement: t = {{12.0, 9.5, 7.5, 5.8, 4.5, 3.5, 2.6, 1.9, 1.2}, true}; return true;
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
