// rank.h - Estimated Valorant-style aim rank (Iron 1 ... Radiant).
//
// This is an estimate of *aim* only, calculated from trainer stats. It is not
// an official rank: real matchmaking rank also depends on game sense,
// utility, positioning and teamwork.
//
// How it works:
//   * Each mode turns one run into a "skill value" (e.g. kills per second
//     weighted by accuracy, time-on-target %, reaction ms).
//   * The value is placed between per-mode tier thresholds, giving a
//     continuous "rank points" number: 0 = Iron 1, 3 = Bronze 1, ...,
//     21 = Immortal 1, 24 = Radiant.
//   * A mode's rank is the median of its last 5 ranked runs; the overall
//     rank is the average over all modes played (needs 3 or more modes).
#pragma once

#include <string>

#include "raylib.h"
#include "stats.h"

constexpr int kTierCount = 9;          // Iron .. Radiant
constexpr double kRadiantPoints = 24.0;
constexpr int kMinModesForOverall = 3;
constexpr double kMinRankedSeconds = 20.0;  // shorter runs are too noisy to rank

struct AimRank {
    int tier = 0;           // 0 = Iron ... 8 = Radiant
    int division = 1;       // 1..3 (Radiant has none)
    double points = 0.0;    // continuous 0..24
    double progress = 0.0;  // 0..1 towards the next division
};

const char* TierName(int tier);
Color TierColor(int tier);
std::string RankLabel(const AimRank& r);  // "Gold 2", "Radiant"

AimRank RankFromPoints(double points);
// Skill value for a run (higher is better; Reaction is inverted internally).
// Returns false when the run cannot be ranked (sens finder test, too short,
// too few hits).
bool RankFromRecord(const RunRecord& r, AimRank& out);
// Median of the last 5 ranked runs of a mode.
bool ModeRank(const StatsStore& stats, ModeId mode, AimRank& out, int* rankedRuns = nullptr);
// Average over modes that have a rank. Needs kMinModesForOverall modes.
bool OverallRank(const StatsStore& stats, AimRank& out, int* modesUsed = nullptr);
