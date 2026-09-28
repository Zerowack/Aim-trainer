// stats.cpp - derived stats, tips and CSV persistence.
#include "stats.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {

const char* const kCsvHeader =
    "timestamp,mode,duration_s,score,accuracy_pct,hits,misses,avg_reaction_ms,avg_ttk_ms,overshoot_pct,"
    "tracking_pct,sens,dpi,difficulty,placement_err_deg,weapon,rounds_won,rounds_lost,bot_tier,kills,deaths,peeks";

std::vector<std::string> SplitCsv(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else if (c != '\r') {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

double ToD(const std::string& s, double def) {
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    return end == s.c_str() ? def : v;
}

std::string Fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

}  // namespace

const char* ModeName(ModeId m) {
    switch (m) {
        case ModeId::Gridshot: return "Gridshot";
        case ModeId::Microshot: return "Microshot";
        case ModeId::Tracking: return "Tracking";
        case ModeId::Flick180: return "Flick 180";
        case ModeId::Reaction: return "Reaction";
        case ModeId::Peek: return "Peek Practice";
        case ModeId::Placement: return "Crosshair Placement";
        case ModeId::Sniper: return "Sniper";
        case ModeId::VsBot: return "VS Bot";
        case ModeId::Headshot: return "Headshot";
        case ModeId::Sixshot: return "Sixshot";
        case ModeId::Spidershot: return "Spidershot";
        case ModeId::Motionshot: return "Motionshot";
        case ModeId::SmoothTrack: return "Smooth Tracking";
        case ModeId::StrafeTap: return "Strafe Tap";
        case ModeId::TargetSwitch: return "Target Switch";
        case ModeId::LongRange: return "Long Range";
        case ModeId::Microflex: return "Microflex";
        case ModeId::Popcorn: return "Popcorn";
        case ModeId::Mixed: return "Sens Finder Test";
        default: return "?";
    }
}

const char* ModeShortName(ModeId m) {
    if (m == ModeId::Placement) return "Placement";
    if (m == ModeId::Peek) return "Peek";
    if (m == ModeId::SmoothTrack) return "Smooth";
    if (m == ModeId::TargetSwitch) return "Switch";
    if (m == ModeId::LongRange) return "Long";
    if (m == ModeId::Spidershot) return "Spider";
    if (m == ModeId::Motionshot) return "Motion";
    return ModeName(m);
}

const char* ModeKey(ModeId m) {
    switch (m) {
        case ModeId::Gridshot: return "gridshot";
        case ModeId::Microshot: return "microshot";
        case ModeId::Tracking: return "tracking";
        case ModeId::Flick180: return "flick180";
        case ModeId::Reaction: return "reaction";
        case ModeId::Peek: return "peek";
        case ModeId::Placement: return "placement";
        case ModeId::Sniper: return "sniper";
        case ModeId::VsBot: return "vsbot";
        case ModeId::Headshot: return "headshot";
        case ModeId::Sixshot: return "sixshot";
        case ModeId::Spidershot: return "spidershot";
        case ModeId::Motionshot: return "motionshot";
        case ModeId::SmoothTrack: return "smoothtrack";
        case ModeId::StrafeTap: return "strafetap";
        case ModeId::TargetSwitch: return "targetswitch";
        case ModeId::LongRange: return "longrange";
        case ModeId::Microflex: return "microflex";
        case ModeId::Popcorn: return "popcorn";
        case ModeId::Mixed: return "mixed";
        default: return "unknown";
    }
}

const char* ModeDescription(ModeId m) {
    switch (m) {
        case ModeId::Gridshot: return "Three targets on a grid. Destroy one and another appears. Pure speed + accuracy.";
        case ModeId::Microshot: return "Tiny head-sized targets that vanish fast. Small, precise flicks.";
        case ModeId::Tracking: return "Hold fire on an agent doing ADAD strafes. Scored by time on target.";
        case ModeId::Flick180: return "Targets spawn beside or behind you. Big turns, clean stops.";
        case ModeId::Reaction: return "Wait for the target, then click as fast as you can. Measured in ms.";
        case ModeId::Peek: return "Agents peek from behind cover for a split second. Hold the angle.";
        case ModeId::Placement: return "An angle lights up, then an agent peeks it. Pre-aim the edge at head height.";
        case ModeId::Sniper: return "Hold a long angle like C long. Enemies swing, jump and crouch peek. Stop to shoot.";
        case ModeId::VsBot: return "1v1 duel against a bot from Iron to Radiant. Move, crouch, counter-strafe, first to 5.";
        case ModeId::Headshot: return "Agents appear around you. Only headshots count - body shots are misses.";
        case ModeId::Sixshot: return "Six small targets on a wall. Destroy one and another appears. Fast small flicks.";
        case ModeId::Spidershot: return "Flick out to a target, then back to the centre. Repeat. Flick + reset.";
        case ModeId::Motionshot: return "Three targets drift across the wall. Click them while they move.";
        case ModeId::SmoothTrack: return "Hold fire on a target moving in smooth curves. Pure tracking control.";
        case ModeId::StrafeTap: return "One-tap the head of an agent doing ADAD strafes. Only headshots count.";
        case ModeId::TargetSwitch: return "Three strafing agents. Track one until it dies, switch to the next.";
        case ModeId::LongRange: return "Tiny targets 30-45 m away. Slow down and land the precise click.";
        case ModeId::Microflex: return "Tiny targets pop up right next to your crosshair. Micro-adjustments.";
        case ModeId::Popcorn: return "Targets are thrown into the air and fall. Hit them before they land.";
        case ModeId::Mixed: return "20 s of flicks, tracking and micro-adjustments.";
        default: return "";
    }
}

ModeCategory CategoryOf(ModeId m) {
    switch (m) {
        case ModeId::Gridshot:
        case ModeId::Flick180:
        case ModeId::Sixshot:
        case ModeId::Spidershot:
        case ModeId::Motionshot:
        case ModeId::Popcorn: return ModeCategory::Flicking;
        case ModeId::Microshot:
        case ModeId::Headshot:
        case ModeId::StrafeTap:
        case ModeId::LongRange:
        case ModeId::Microflex: return ModeCategory::Precision;
        case ModeId::Tracking:
        case ModeId::SmoothTrack:
        case ModeId::TargetSwitch: return ModeCategory::Tracking;
        case ModeId::Reaction: return ModeCategory::Reaction;
        default: return ModeCategory::Valorant;
    }
}

const char* CategoryName(ModeCategory c) {
    switch (c) {
        case ModeCategory::Flicking: return "Flicking";
        case ModeCategory::Precision: return "Precision";
        case ModeCategory::Tracking: return "Tracking";
        case ModeCategory::Reaction: return "Reaction";
        default: return "Valorant";
    }
}

const char* ModeTags(ModeId m) {
    switch (m) {
        case ModeId::Gridshot: return "click speed grid static wall warmup";
        case ModeId::Microshot: return "micro small precision one bullet head";
        case ModeId::Tracking: return "track strafe adad agent hold fire";
        case ModeId::Flick180: return "flick turn behind 180 big";
        case ModeId::Reaction: return "reflex ms time click fast";
        case ModeId::Peek: return "peek cover angle hold headshot";
        case ModeId::Placement: return "crosshair placement pre aim head level";
        case ModeId::Sniper: return "operator op marshal outlaw scope awp peek angle";
        case ModeId::VsBot: return "bot duel 1v1 sheriff skirmish movement counter strafe";
        case ModeId::Headshot: return "head only agents one tap click";
        case ModeId::Sixshot: return "six small click speed wall grid";
        case ModeId::Spidershot: return "spider flick centre center reset";
        case ModeId::Motionshot: return "moving targets click motion";
        case ModeId::SmoothTrack: return "smooth track tracking sphere curve smoothbot";
        case ModeId::StrafeTap: return "strafe adad one tap head click agent";
        case ModeId::TargetSwitch: return "switch switching tracking multiple agents";
        case ModeId::LongRange: return "long range far tiny precision sniper";
        case ModeId::Microflex: return "micro flex small close adjust";
        case ModeId::Popcorn: return "popcorn air falling vertical moving";
        default: return "";
    }
}

bool IsTrackingMode(ModeId m) {
    return m == ModeId::Tracking || m == ModeId::SmoothTrack || m == ModeId::TargetSwitch;
}

bool ModeFromKey(const std::string& key, ModeId& out) {
    for (int i = 0; i < static_cast<int>(ModeId::Count); ++i) {
        if (key == ModeKey(static_cast<ModeId>(i))) {
            out = static_cast<ModeId>(i);
            return true;
        }
    }
    return false;
}

double RunStats::Accuracy() const {
    if (IsTrackingMode(mode)) return trackHeldTime > 0.0 ? trackOnTime / trackHeldTime : 0.0;
    return shots > 0 ? static_cast<double>(hits) / shots : 0.0;
}

double RunStats::AvgReactionMs() const { return reactionCount > 0 ? reactionSumMs / reactionCount : -1.0; }
double RunStats::AvgTtkMs() const { return ttkCount > 0 ? ttkSumMs / ttkCount : -1.0; }
double RunStats::TrackingPct() const { return trackTotalTime > 0.0 ? trackOnTime / trackTotalTime : -1.0; }

double RunStats::OvershootShare() const {
    const int n = overshoots + undershoots;
    return n > 0 ? static_cast<double>(overshoots) / n : -1.0;
}

double RunStats::MeanRelError() const { return relErrorCount > 0 ? relErrorSum / relErrorCount : 0.0; }
double RunStats::MeanErrNorm() const { return errCount > 0 ? errNormSum / errCount : -1.0; }
double RunStats::MeanErrDeg() const { return errCount > 0 ? errDegSum / errCount : -1.0; }
double RunStats::MeanPlacementErr() const { return placementCount > 0 ? placementErrSum / placementCount : -1.0; }
double RunStats::MeanPlacementVert() const { return placementCount > 0 ? placementVertSum / placementCount : 0.0; }
double RunStats::HeadLevelPct() const { return headLevelTotal > 0.0 ? headLevelTime / headLevelTotal : -1.0; }

const char* SniperName(SniperWeapon w) {
    switch (w) {
        case SniperWeapon::Marshal: return "Marshal";
        case SniperWeapon::Outlaw: return "Outlaw";
        default: return "Operator";
    }
}

const char* SniperKey(SniperWeapon w) {
    switch (w) {
        case SniperWeapon::Marshal: return "marshal";
        case SniperWeapon::Outlaw: return "outlaw";
        default: return "operator";
    }
}

bool SniperFromKey(const std::string& key, SniperWeapon& out) {
    for (int i = 0; i < kSniperCount; ++i) {
        if (key == SniperKey(static_cast<SniperWeapon>(i))) {
            out = static_cast<SniperWeapon>(i);
            return true;
        }
    }
    return false;
}

DifficultyParams GetDifficulty(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return {1.35f, 1.40f, 0.75f, 0.75};
        case Difficulty::Hard: return {0.78f, 0.78f, 1.20f, 1.25};
        case Difficulty::Insane: return {0.60f, 0.60f, 1.40f, 1.50};
        default: return {1.0f, 1.0f, 1.0f, 1.0};
    }
}

const char* DifficultyName(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return "Easy";
        case Difficulty::Hard: return "Hard";
        case Difficulty::Insane: return "Insane";
        default: return "Normal";
    }
}

const char* DifficultyKey(Difficulty d) {
    switch (d) {
        case Difficulty::Easy: return "easy";
        case Difficulty::Hard: return "hard";
        case Difficulty::Insane: return "insane";
        default: return "normal";
    }
}

bool DifficultyFromKey(const std::string& key, Difficulty& out) {
    for (int i = 0; i < kDifficultyCount; ++i) {
        if (key == DifficultyKey(static_cast<Difficulty>(i))) {
            out = static_cast<Difficulty>(i);
            return true;
        }
    }
    return false;
}

int SuggestedSensChangePct(const RunStats& s) {
    const int dirShots = s.overshoots + s.undershoots;
    if (dirShots < 6 || s.relErrorCount < 6) return 0;
    const double share = s.OvershootShare();
    const double rel = s.MeanRelError();  // + overshoot, - undershoot
    int pct = static_cast<int>(std::lround(std::fabs(rel) * 100.0));
    if (pct < 2) pct = 2;
    if (pct > 15) pct = 15;
    if (share >= 0.62 && rel > 0.015) return -pct;
    if (share <= 0.38 && rel < -0.015) return pct;
    return 0;
}

std::vector<std::string> BuildTips(const RunStats& s, double sens) {
    std::vector<std::string> tips;

    // 1) Over/undershoot tendency -> concrete sensitivity suggestion.
    const int dirShots = s.overshoots + s.undershoots;
    if (dirShots >= 6 && s.relErrorCount >= 6) {
        const double share = s.OvershootShare();
        const int change = SuggestedSensChangePct(s);
        const int pct = change < 0 ? -change : change;
        if (change < 0) {
            const double suggested = sens * (1.0 - pct / 100.0);
            tips.push_back("You overshoot flicks (" + Fmt(share * 100.0, 0) + "% of directional misses). Try lowering sens ~" +
                           std::to_string(pct) + "% (" + Fmt(sens, 3) + " -> " + Fmt(suggested, 3) + ").");
        } else if (change > 0) {
            const double suggested = sens * (1.0 + pct / 100.0);
            tips.push_back("You undershoot flicks (" + Fmt((1.0 - share) * 100.0, 0) +
                           "% of directional misses). Try raising sens ~" + std::to_string(pct) + "% (" + Fmt(sens, 3) +
                           " -> " + Fmt(suggested, 3) + ").");
        } else {
            tips.push_back("Over- and undershoots are balanced (" + Fmt(share * 100.0, 0) +
                           "% over). Your sens fits your flick distance well.");
        }
    }

    // 2) Accuracy vs speed trade-off.
    if (!IsTrackingMode(s.mode) && s.shots >= 5) {
        const double acc = s.Accuracy();
        const double ttk = s.AvgTtkMs();
        if (acc < 0.6) {
            tips.push_back("Accuracy is " + Fmt(acc * 100.0, 0) +
                           "%. Slow down ~10% and only click once the crosshair is settled on the target.");
        } else if (acc > 0.92 && ttk > 0.0 && ttk > 550.0) {
            tips.push_back("Very accurate (" + Fmt(acc * 100.0, 0) + "%) but slow (" + Fmt(ttk, 0) +
                           " ms per kill). Push your speed - some misses are fine while you train.");
        }
    }

    // 3) Tracking.
    const double track = s.TrackingPct();
    if (track >= 0.0) {
        if (track < 0.35) {
            tips.push_back("On target " + Fmt(track * 100.0, 0) +
                           "% of the time. Focus on matching the strafe speed instead of chasing - react to direction changes.");
        } else if (track < 0.6) {
            tips.push_back("Decent tracking (" + Fmt(track * 100.0, 0) +
                           "%). Watch the target's body, not your crosshair, to anticipate ADAD swaps.");
        } else {
            tips.push_back("Strong tracking (" + Fmt(track * 100.0, 0) + "% on target).");
        }
    }

    // 4) Reaction.
    const double rt = s.AvgReactionMs();
    if (rt > 0.0 && (s.mode == ModeId::Reaction || s.mode == ModeId::Peek)) {
        if (rt > 320.0) tips.push_back("Average reaction " + Fmt(rt, 0) + " ms. Stay relaxed and look at the exact spot the target will appear.");
        else if (rt < 220.0) tips.push_back("Excellent reaction time (" + Fmt(rt, 0) + " ms).");
    }

    // 5) VS Bot: movement discipline.
    if ((s.mode == ModeId::VsBot && s.shots >= 10) || (s.mode == ModeId::Sniper && s.shots >= 5)) {
        const double moving = static_cast<double>(s.movingShots) / s.shots;
        if (moving > 0.25) {
            tips.push_back(Fmt(moving * 100.0, 0) +
                           "% of your shots were fired while moving - they fly wide. Counter-strafe: tap the opposite "
                           "key (A after D) and shoot the instant you stop.");
        } else {
            tips.push_back("Good discipline: only " + Fmt(moving * 100.0, 0) + "% of shots fired while moving.");
        }
        const double hs = s.hits > 0 ? static_cast<double>(s.headshots) / s.hits : 0.0;
        if (s.mode == ModeId::VsBot && s.hits >= 5 && hs < 0.2) {
            tips.push_back("Only " + Fmt(hs * 100.0, 0) +
                           "% headshots. Keep your crosshair at head height - a single Vandal headshot kills.");
        }
    }

    // Sniper: angle holding.
    if (s.mode == ModeId::Sniper && s.peeks >= 3) {
        const int escaped = s.peeks - s.kills - s.deaths;
        if (escaped * 3 > s.peeks) {
            tips.push_back(std::to_string(std::max(0, escaped)) + " of " + std::to_string(s.peeks) +
                           " peeks got away. Hold your crosshair on the edge of the cover at head height, so the peek "
                           "runs into your crosshair instead of you chasing it.");
        }
        if (s.deaths >= 3) {
            tips.push_back("You died " + std::to_string(s.deaths) +
                           " times. Don't stand in the open while the bolt cycles or you reload: step back into "
                           "cover, then re-peek from a different spot.");
        }
    }

    // 6) Crosshair placement.
    const double place = s.MeanPlacementErr();
    if (place >= 0.0 && s.placementCount >= 5) {
        const double vert = s.MeanPlacementVert();
        if (vert < -1.0) {
            tips.push_back("Your crosshair sits " + Fmt(-vert, 1) +
                           " deg below head level on average. Raise it: in Valorant heads are at your eye line.");
        } else if (vert > 1.0) {
            tips.push_back("Your crosshair sits " + Fmt(vert, 1) + " deg above head level. Lower it slightly.");
        }
        if (place > 3.5) {
            tips.push_back("Placement error " + Fmt(place, 1) +
                           " deg. Pre-aim the edge of cover where agents can appear, not the middle of the wall.");
        } else if (place < 1.5) {
            tips.push_back("Great placement (" + Fmt(place, 1) + " deg). Most kills should need only a micro-adjustment.");
        }
        const double hl = s.HeadLevelPct();
        if (hl >= 0.0 && hl < 0.5) {
            tips.push_back("Crosshair at head level only " + Fmt(hl * 100.0, 0) +
                           "% of the time. Move it to head height of the lit angle before the agent peeks (ledges are higher).");
        }
    }

    // 7) Precision.
    const double errNorm = s.MeanErrNorm();
    if (errNorm > 1.3 && s.errCount >= 8) {
        tips.push_back("Clicks land " + Fmt(errNorm, 1) +
                       "x the target radius from centre on average. Practise Microshot to tighten micro-adjustments.");
    }
    if (s.expired > 0 && s.hits > 0 && s.expired * 3 > s.hits) {
        tips.push_back(std::to_string(s.expired) + " targets got away. Commit to the shot earlier.");
    }

    if (tips.empty()) {
        tips.push_back(s.shots + static_cast<int>(s.trackTotalTime) < 5
                           ? "Not enough data for detailed tips yet - play a full run."
                           : "Solid run. Keep your sens consistent and repeat this mode to build muscle memory.");
    }
    return tips;
}

RunRecord MakeRecord(const RunStats& s, double sens, double dpi) {
    RunRecord r;
    r.timestamp = NowTimestamp();
    r.mode = s.mode;
    r.difficulty = s.difficulty;
    r.weapon = s.weapon;
    r.duration = s.duration;
    r.placementErr = s.MeanPlacementErr();
    if (s.mode == ModeId::VsBot) {
        r.roundsWon = s.roundsWon;
        r.roundsLost = s.roundsLost;
        r.botTier = s.botTier;
    }
    if (s.mode == ModeId::VsBot || s.mode == ModeId::Sniper) {
        r.kills = s.kills;
        r.deaths = s.deaths;
    }
    if (s.mode == ModeId::Sniper) r.peeks = s.peeks;
    r.score = s.score;
    r.accuracy = s.Accuracy() * 100.0;
    r.hits = s.hits;
    r.misses = s.Misses();
    r.avgReactionMs = s.AvgReactionMs();
    r.avgTtkMs = s.AvgTtkMs();
    const double share = s.OvershootShare();
    r.overshootPct = share >= 0.0 ? share * 100.0 : -1.0;
    const double track = s.TrackingPct();
    r.trackingPct = track >= 0.0 ? track * 100.0 : -1.0;
    r.sens = sens;
    r.dpi = dpi;
    return r;
}

std::string NowTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm = {};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return buf;
}

void StatsStore::Load(const std::string& path) {
    path_ = path;
    records_.clear();
    std::ifstream in(path);
    if (!in) return;
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) {
            header = false;
            if (line.rfind("timestamp", 0) == 0) continue;
        }
        const std::vector<std::string> f = SplitCsv(line);
        if (f.size() < 13) continue;
        RunRecord r;
        r.timestamp = f[0];
        if (!ModeFromKey(f[1], r.mode)) continue;
        r.duration = ToD(f[2], 0.0);
        r.score = static_cast<long long>(ToD(f[3], 0.0));
        r.accuracy = ToD(f[4], 0.0);
        r.hits = static_cast<int>(ToD(f[5], 0.0));
        r.misses = static_cast<int>(ToD(f[6], 0.0));
        r.avgReactionMs = ToD(f[7], -1.0);
        r.avgTtkMs = ToD(f[8], -1.0);
        r.overshootPct = ToD(f[9], -1.0);
        r.trackingPct = ToD(f[10], -1.0);
        r.sens = ToD(f[11], 0.0);
        r.dpi = ToD(f[12], 0.0);
        // Columns added in v1.6; older rows default to Normal.
        if (f.size() >= 14) DifficultyFromKey(f[13], r.difficulty);
        if (f.size() >= 15) r.placementErr = ToD(f[14], -1.0);
        if (f.size() >= 16) SniperFromKey(f[15], r.weapon);
        if (f.size() >= 19) {
            r.roundsWon = static_cast<int>(ToD(f[16], -1.0));
            r.roundsLost = static_cast<int>(ToD(f[17], -1.0));
            r.botTier = static_cast<int>(ToD(f[18], -1.0));
        }
        // Columns added in v1.9.
        if (f.size() >= 22) {
            r.kills = static_cast<int>(ToD(f[19], -1.0));
            r.deaths = static_cast<int>(ToD(f[20], -1.0));
            r.peeks = static_cast<int>(ToD(f[21], -1.0));
        }
        records_.push_back(r);
    }
}

bool StatsStore::Append(const RunRecord& r) {
    records_.push_back(r);
    bool needHeader = false;
    {
        std::ifstream probe(path_);
        needHeader = !probe || probe.peek() == std::ifstream::traits_type::eof();
    }
    std::ofstream out(path_, std::ios::app);
    if (!out) return false;
    if (needHeader) out << kCsvHeader << "\n";
    out << r.timestamp << ',' << ModeKey(r.mode) << ',' << Fmt(r.duration, 2) << ',' << r.score << ','
        << Fmt(r.accuracy, 2) << ',' << r.hits << ',' << r.misses << ',' << Fmt(r.avgReactionMs, 1) << ','
        << Fmt(r.avgTtkMs, 1) << ',' << Fmt(r.overshootPct, 1) << ',' << Fmt(r.trackingPct, 2) << ','
        << Fmt(r.sens, 4) << ',' << Fmt(r.dpi, 0) << ',' << DifficultyKey(r.difficulty) << ','
        << Fmt(r.placementErr, 2) << ',' << (r.mode == ModeId::Sniper ? SniperKey(r.weapon) : "-") << ',' << r.roundsWon
        << ',' << r.roundsLost << ',' << r.botTier << ',' << r.kills << ',' << r.deaths << ',' << r.peeks << "\n";
    return static_cast<bool>(out);
}

std::vector<const RunRecord*> StatsStore::ForMode(ModeId m) const {
    std::vector<const RunRecord*> out;
    for (const RunRecord& r : records_) {
        if (r.mode == m) out.push_back(&r);
    }
    return out;
}

bool StatsStore::BestScore(ModeId m, Difficulty d, long long& out) const {
    bool any = false;
    for (const RunRecord& r : records_) {
        if (r.mode != m || r.difficulty != d) continue;
        if (!any || r.score > out) out = r.score;
        any = true;
    }
    return any;
}

bool StatsStore::BestScore(ModeId m, long long& out) const {
    bool any = false;
    for (const RunRecord& r : records_) {
        if (r.mode != m) continue;
        if (!any || r.score > out) out = r.score;
        any = true;
    }
    return any;
}
