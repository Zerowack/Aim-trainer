// training.h - Warmup routine and the adaptive "Improve My Aim" coach.
//
// Both are playlists of normal runs played back to back. The coach reads
// your saved runs (stats.csv), scores five skills from your recent ranked
// runs, finds your weakest areas and habits (overshooting, low accuracy),
// and builds a plan of tasks for them. Every task's difficulty follows your
// current rank in that mode, so plans get harder as you improve, and the
// next plan is built from the new results.
#pragma once

#include <string>
#include <vector>

#include "rng.h"
#include "stats.h"

enum class PlanKind { Warmup, Improve };

struct PlanStep {
    ModeId mode = ModeId::Gridshot;
    Difficulty difficulty = Difficulty::Normal;
    int seconds = 45;
    std::string why;  // shown before the task
};

enum class Skill : int { Flicking = 0, Precision, Tracking, Reaction, Placement, Count };
constexpr int kSkillCount = 5;
const char* SkillName(Skill s);

struct SkillScore {
    double points = -1.0;  // rank points 0..24 (average of the skill's ranked modes), -1 = not tested yet
    int modes = 0;         // ranked modes that went into it
};

struct Diagnosis {
    SkillScore skills[kSkillCount];
    double overshootShare = -1.0;  // recent over / (over + under) in click modes, -1 = not enough data
    double clickAccuracy = -1.0;   // recent accuracy in click modes (0..1), -1 = no data
    int recentRuns = 0;
    // Plain-language findings, most important first.
    std::vector<std::string> findings;
};

Diagnosis Diagnose(const StatsStore& stats);

// ~5 minutes: easy big targets first, then tracking, flicks, micro-adjustments,
// heads and reaction.
std::vector<PlanStep> BuildWarmup();

// A plan for the weakest skills (or an assessment when there is little data).
std::vector<PlanStep> BuildImprovePlan(const Diagnosis& d, const StatsStore& stats, Rng& rng);

// Difficulty that fits your current rank in a mode (Easy below Silver,
// Normal to Platinum, Hard to Ascendant, Insane above).
Difficulty DifficultyForMode(const StatsStore& stats, ModeId mode);
