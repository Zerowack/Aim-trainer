// stats.h - Per-run statistics, coaching tips and the persistent stats file.
#pragma once

#include <string>
#include <vector>

enum class ModeId : int { Gridshot = 0, Microshot, Tracking, Flick180, Reaction, Peek, Placement, Sniper, VsBot, Mixed, Count };

constexpr int kPlayableModeCount = 9;  // everything except Mixed (sens finder only)

// Sniper rifles for the Sniper mode.
enum class SniperWeapon : int { Marshal = 0, Outlaw, Operator, Count };
constexpr int kSniperCount = 3;
const char* SniperName(SniperWeapon w);  // "Operator"
const char* SniperKey(SniperWeapon w);   // "operator" (CSV)
bool SniperFromKey(const std::string& key, SniperWeapon& out);

// Difficulty picked before each run. It scales target size, time windows,
// movement speed and distance, and the rank value (harder = worth more).
enum class Difficulty : int { Easy = 0, Normal, Hard, Insane, Count };
constexpr int kDifficultyCount = 4;

struct DifficultyParams {
    float size;       // target size multiplier (humanoids: 1/size distance)
    float time;       // lifetime / exposure window multiplier
    float speed;      // movement speed multiplier
    double rankMult;  // rank value multiplier
};

DifficultyParams GetDifficulty(Difficulty d);
const char* DifficultyName(Difficulty d);  // "Hard"
const char* DifficultyKey(Difficulty d);   // "hard" (CSV)
bool DifficultyFromKey(const std::string& key, Difficulty& out);

const char* ModeName(ModeId m);         // "Gridshot"
const char* ModeShortName(ModeId m);    // short label for tabs ("Placement")
const char* ModeKey(ModeId m);          // "gridshot" (used in the CSV file)
const char* ModeDescription(ModeId m);
bool ModeFromKey(const std::string& key, ModeId& out);

// Raw counters collected during a run. Derived values are computed on demand.
struct RunStats {
    ModeId mode = ModeId::Gridshot;
    Difficulty difficulty = Difficulty::Normal;
    SniperWeapon weapon = SniperWeapon::Operator;  // Sniper mode only
    double duration = 0.0;  // seconds actually played

    long long score = 0;
    int shots = 0;
    int hits = 0;
    int headshots = 0;
    int expired = 0;  // targets that timed out / peeks that got away

    double reactionSumMs = 0.0;
    int reactionCount = 0;
    double ttkSumMs = 0.0;
    int ttkCount = 0;

    // Shot error along the direction the crosshair was moving at the click.
    int overshoots = 0;
    int undershoots = 0;
    int centered = 0;
    double relErrorSum = 0.0;  // signed error / flick distance (+ = overshoot)
    int relErrorCount = 0;
    double errNormSum = 0.0;   // |error| / target angular radius
    double errDegSum = 0.0;    // |error| in degrees
    int errCount = 0;

    // Tracking
    double trackOnTime = 0.0;     // firing and on target
    double trackHeldTime = 0.0;   // firing
    double trackTotalTime = 0.0;  // time a tracking target existed

    // Crosshair placement: angle from crosshair to the head at the moment
    // each agent appeared, and time spent with the crosshair at head level.
    double placementErrSum = 0.0;
    double placementVertSum = 0.0;  // signed: negative = crosshair below head
    int placementCount = 0;
    double headLevelTime = 0.0;
    double headLevelTotal = 0.0;

    // VS Bot matches.
    int botTier = 3;         // 0 Iron .. 8 Radiant
    int roundsWon = 0;
    int roundsLost = 0;
    int kills = 0;
    int deaths = 0;
    double damageDealt = 0.0;
    double damageTaken = 0.0;
    int movingShots = 0;     // shots fired while moving too fast to be accurate
    int peeks = 0;           // Sniper: enemies that peeked (killed or got away)

    int Misses() const { return shots - hits; }
    double Accuracy() const;            // 0..1 (tracking uses on-target / firing time)
    double AvgReactionMs() const;       // < 0 if no data
    double AvgTtkMs() const;            // < 0 if no data
    double TrackingPct() const;         // 0..1, < 0 if no tracking in this run
    double OvershootShare() const;      // overshoots / (overshoots + undershoots), < 0 if no data
    double MeanRelError() const;        // signed, + = overshoot
    double MeanErrNorm() const;         // < 0 if no data
    double MeanErrDeg() const;          // < 0 if no data
    double MeanPlacementErr() const;    // degrees, < 0 if no data
    double MeanPlacementVert() const;   // degrees, signed
    double HeadLevelPct() const;        // 0..1, < 0 if no data
};

// The coach's sensitivity suggestion from over/undershoot data, in percent
// (e.g. -6 = lower sens by 6%). 0 when there is not enough data or the
// tendency is balanced.
int SuggestedSensChangePct(const RunStats& s);

// Data-driven coaching tips for the results screen.
std::vector<std::string> BuildTips(const RunStats& s, double sens);

// One row of stats.csv.
struct RunRecord {
    std::string timestamp;
    ModeId mode = ModeId::Gridshot;
    Difficulty difficulty = Difficulty::Normal;
    SniperWeapon weapon = SniperWeapon::Operator;  // meaningful for Sniper runs only
    double duration = 0.0;
    long long score = 0;
    double accuracy = 0.0;      // percent
    int hits = 0;
    int misses = 0;
    double avgReactionMs = -1.0;
    double avgTtkMs = -1.0;
    double overshootPct = -1.0;  // share of over- vs undershoots, percent
    double trackingPct = -1.0;
    double sens = 0.0;
    double dpi = 0.0;
    double placementErr = -1.0;  // Crosshair Placement mode, degrees
    int roundsWon = -1;          // VS Bot only
    int roundsLost = -1;
    int botTier = -1;
    int kills = -1;              // VS Bot and Sniper
    int deaths = -1;
    int peeks = -1;              // Sniper only
};

RunRecord MakeRecord(const RunStats& s, double sens, double dpi);

// "2026-09-25 14:03:12"
std::string NowTimestamp();

class StatsStore {
public:
    void Load(const std::string& path);
    bool Append(const RunRecord& r);

    const std::vector<RunRecord>& Records() const { return records_; }
    std::vector<const RunRecord*> ForMode(ModeId m) const;
    // Best score for the mode, or false if there are no runs.
    bool BestScore(ModeId m, long long& out) const;
    // Best score for the mode at one difficulty.
    bool BestScore(ModeId m, Difficulty d, long long& out) const;

private:
    std::string path_;
    std::vector<RunRecord> records_;
};
