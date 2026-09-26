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
        // Gridshot: kills per second x sqrt(accuracy).
        case ModeId::Gridshot: t = {{0.9, 1.3, 1.7, 2.1, 2.5, 2.9, 3.3, 3.8, 4.4}, false}; return true;
        // Microshot: head-size kills per second x sqrt(accuracy).
        case ModeId::Microshot: t = {{0.30, 0.45, 0.60, 0.75, 0.90, 1.05, 1.20, 1.40, 1.65}, false}; return true;
        // Tracking: time on target, percent.
        case ModeId::Tracking: t = {{20.0, 27.0, 34.0, 41.0, 48.0, 55.0, 62.0, 69.0, 77.0}, false}; return true;
        // Flick 180: kills per second x sqrt(accuracy).
        case ModeId::Flick180: t = {{0.25, 0.36, 0.48, 0.60, 0.72, 0.84, 0.96, 1.10, 1.26}, false}; return true;
        // Reaction: average ms (includes your monitor/system latency).
        case ModeId::Reaction: t = {{340.0, 315.0, 295.0, 275.0, 258.0, 243.0, 229.0, 215.0, 200.0}, true}; return true;
        // Peek: peeks killed per second x sqrt(accuracy).
        case ModeId::Peek: t = {{0.10, 0.15, 0.20, 0.25, 0.30, 0.35, 0.40, 0.45, 0.50}, false}; return true;
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
        "Your aim is dogshit. The targets are literally standing still, bro.",
        "Just delete the game, it's not for u lil bro.",
        "Were you aiming with your monitor turned off?",
        "Even the practice bots feel bad for you.",
        "Uninstall. Go outside. Find a new hobby. Maybe knitting.",
    };
    static const char* const bronze[] = {
        "Bronze aim. You shoot like the mouse is upside down.",
        "Your crosshair has serious commitment issues.",
        "Hardstuck energy detected. Keep grinding, lil bro.",
        "You miss more than a stormtrooper at a family reunion.",
    };
    static const char* const silver[] = {
        "Silver: you hit shots... occasionally... mostly by accident.",
        "Not bad. Not good. Just... there.",
        "Aim of a Jett main who forgot she can dash.",
        "The enemy team isn't scared, but they're not laughing either.",
    };
    static const char* const gold[] = {
        "Gold. Perfectly average. The human definition of 'mid'.",
        "Your aim won't lose you games. It won't win them either.",
        "Solid. Now stop crouch-spraying across the map.",
    };
    static const char* const plat[] = {
        "Platinum hands. Now fix that crosshair placement.",
        "Decent. Your teammates might finally stop typing 'diff'.",
        "You're getting dangerous. Mildly dangerous.",
    };
    static const char* const diamond[] = {
        "Diamond aim. Clean. The enemy is checking your tracker.",
        "Crisp flicks. Your mouse pad fears you.",
        "Okay, you're actually good. Don't let it go to your head.",
    };
    static const char* const ascendant[] = {
        "Ascendant aim. Stop playing aim trainers and go rank up.",
        "Cracked. Someone is typing 'reported' in all chat right now.",
        "Your aim is doing all the carrying. Hope your brain catches up.",
    };
    static const char* const immortal[] = {
        "Immortal aim. Reported for aimbot, probably.",
        "Your mouse deserves a raise and paid holidays.",
        "Heads are just magnets for your crosshair at this point.",
    };
    static const char* const radiant[] = {
        "Radiant aim. Are you a pro or is your crosshair glued to heads?",
        "Touch grass. Please. You've peaked.",
        "Absolutely inhuman. Go sign a contract.",
    };
    struct List {
        const char* const* lines;
        unsigned int count;
    };
    static const List lists[kTierCount] = {{iron, 5}, {bronze, 4}, {silver, 4}, {gold, 3}, {plat, 3},
                                           {diamond, 3}, {ascendant, 3}, {immortal, 3}, {radiant, 3}};
    const List& l = lists[std::max(0, std::min(kTierCount - 1, r.tier))];
    // Iron 1 always gets the classic.
    if (r.tier == 0 && r.division == 1 && (seed % 2u) == 0u) return iron[1];
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
        case ModeId::Tracking:
            if (rec.trackingPct < 0.0) return false;
            value = rec.trackingPct;
            break;
        case ModeId::Reaction:
            if (rec.avgReactionMs <= 0.0 || rec.hits < 3) return false;
            value = rec.avgReactionMs;
            break;
        default:
            if (rec.hits < 5) return false;
            value = static_cast<double>(rec.hits) / rec.duration * std::sqrt(acc);
            break;
    }
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
