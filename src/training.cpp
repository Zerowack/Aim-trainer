// training.cpp - warmup routine and the adaptive "Improve My Aim" coach.
#include "training.h"

#include <algorithm>
#include <cmath>

#include "rank.h"

namespace {

struct SkillPool {
    Skill skill;
    std::vector<ModeId> modes;  // first = the main test for the skill
};

const std::vector<SkillPool>& Pools() {
    static const std::vector<SkillPool> pools = {
        {Skill::Flicking,
         {ModeId::Gridshot, ModeId::Spidershot, ModeId::Sixshot, ModeId::Flick180, ModeId::Motionshot, ModeId::Popcorn}},
        {Skill::Precision, {ModeId::Microshot, ModeId::Headshot, ModeId::Microflex, ModeId::StrafeTap, ModeId::LongRange}},
        {Skill::Tracking, {ModeId::Tracking, ModeId::SmoothTrack, ModeId::TargetSwitch}},
        {Skill::Reaction, {ModeId::Reaction}},
        {Skill::Placement, {ModeId::Placement, ModeId::Peek}},
    };
    return pools;
}

const SkillPool& PoolOf(Skill s) {
    for (const SkillPool& p : Pools()) {
        if (p.skill == s) return p;
    }
    return Pools().front();
}

bool IsClickMode(ModeId m) {
    const ModeCategory c = CategoryOf(m);
    return (c == ModeCategory::Flicking || c == ModeCategory::Precision) && m != ModeId::Reaction;
}

std::string RankWord(double points) {
    return RankLabel(RankFromPoints(points));
}

}  // namespace

const char* SkillName(Skill s) {
    switch (s) {
        case Skill::Flicking: return "Flicking";
        case Skill::Precision: return "Precision";
        case Skill::Tracking: return "Tracking";
        case Skill::Reaction: return "Reaction";
        default: return "Crosshair placement";
    }
}

Difficulty DifficultyForMode(const StatsStore& stats, ModeId mode) {
    AimRank r;
    if (!ModeRank(stats, mode, r)) return Difficulty::Normal;
    if (r.points < 6.0) return Difficulty::Easy;
    if (r.points < 13.0) return Difficulty::Normal;
    if (r.points < 19.0) return Difficulty::Hard;
    return Difficulty::Insane;
}

Diagnosis Diagnose(const StatsStore& stats) {
    Diagnosis d;
    for (const SkillPool& p : Pools()) {
        double sum = 0.0;
        int n = 0;
        for (ModeId m : p.modes) {
            AimRank r;
            if (ModeRank(stats, m, r)) {
                sum += r.points;
                ++n;
            }
        }
        SkillScore& s = d.skills[static_cast<int>(p.skill)];
        s.modes = n;
        s.points = n > 0 ? sum / n : -1.0;
    }

    // Habits from the last 20 click-mode runs.
    const std::vector<RunRecord>& all = stats.Records();
    double over = 0.0, overW = 0.0, acc = 0.0;
    int accN = 0;
    for (auto it = all.rbegin(); it != all.rend() && accN < 20; ++it) {
        if (!IsClickMode(it->mode) || it->duration < 20.0 || it->hits == 0) continue;  // skip idle runs
        acc += it->accuracy / 100.0;
        ++accN;
        if (it->overshootPct >= 0.0) {
            over += it->overshootPct / 100.0;
            overW += 1.0;
        }
    }
    d.recentRuns = accN;
    if (accN > 0) d.clickAccuracy = acc / accN;
    if (overW >= 3.0) d.overshootShare = over / overW;

    // Findings, most important first.
    int weakest = -1;
    for (int i = 0; i < kSkillCount; ++i) {
        if (d.skills[i].points < 0.0) continue;
        if (weakest < 0 || d.skills[i].points < d.skills[weakest].points) weakest = i;
    }
    if (weakest >= 0) {
        d.findings.push_back(std::string("Weakest skill: ") + SkillName(static_cast<Skill>(weakest)) + " (" +
                             RankWord(d.skills[weakest].points) + ").");
    }
    if (d.clickAccuracy >= 0.0 && d.clickAccuracy < 0.65) {
        d.findings.push_back("Accuracy is " + std::to_string(static_cast<int>(std::lround(d.clickAccuracy * 100.0))) +
                             "% in click modes: you shoot before you are on target. Slow down until it's 80%+.");
    }
    if (d.overshootShare > 0.6) {
        d.findings.push_back("You overshoot " + std::to_string(static_cast<int>(std::lround(d.overshootShare * 100.0))) +
                             "% of flicks: work on stopping on the target (or try a slightly lower sens).");
    } else if (d.overshootShare >= 0.0 && d.overshootShare < 0.4) {
        d.findings.push_back("You undershoot " +
                             std::to_string(static_cast<int>(std::lround((1.0 - d.overshootShare) * 100.0))) +
                             "% of flicks: commit to the full flick (or try a slightly higher sens).");
    }
    int untested = 0;
    for (const SkillScore& s : d.skills) untested += s.points < 0.0 ? 1 : 0;
    if (untested > 0) {
        d.findings.push_back(std::to_string(untested) + " skill" + (untested > 1 ? "s" : "") +
                             " not tested yet - the plan includes a test.");
    }
    return d;
}

std::vector<PlanStep> BuildWarmup() {
    return {
        {ModeId::Gridshot, Difficulty::Easy, 40, "Big, easy targets: get your hand and eyes moving."},
        {ModeId::SmoothTrack, Difficulty::Normal, 40, "Smooth tracking: relax your grip and follow the target."},
        {ModeId::Spidershot, Difficulty::Normal, 40, "Flick out and back: clean stops, no overshooting."},
        {ModeId::Microflex, Difficulty::Normal, 35, "Micro-adjustments: tiny, precise corrections."},
        {ModeId::StrafeTap, Difficulty::Normal, 40, "Heads on a strafing agent: one-tap discipline."},
        {ModeId::Headshot, Difficulty::Normal, 40, "Headshots only: crosshair at head height."},
        {ModeId::Reaction, Difficulty::Normal, 30, "Wake up your reactions."},
    };
}

std::vector<PlanStep> BuildImprovePlan(const Diagnosis& d, const StatsStore& stats, Rng& rng) {
    std::vector<PlanStep> plan;
    auto has = [&](ModeId m) {
        for (const PlanStep& s : plan) {
            if (s.mode == m) return true;
        }
        return false;
    };
    auto add = [&](ModeId m, const std::string& why, int seconds = 45) {
        if (has(m) || plan.size() >= 7) return;
        PlanStep s;
        s.mode = m;
        s.difficulty = DifficultyForMode(stats, m);
        s.seconds = seconds;
        s.why = why;
        plan.push_back(s);
    };

    // 1) A short easy opener.
    PlanStep opener{ModeId::Gridshot, Difficulty::Easy, 30, "Warm-up task: easy targets before the real work."};
    plan.push_back(opener);

    // 2) Untested skills first: the coach needs data.
    for (int i = 0; i < kSkillCount; ++i) {
        if (d.skills[i].points >= 0.0) continue;
        const Skill sk = static_cast<Skill>(i);
        add(PoolOf(sk).modes.front(), std::string("Test: ") + SkillName(sk) + " hasn't been measured yet.");
    }

    // 3) The two weakest measured skills: the weakest gets two tasks.
    std::vector<int> order;
    for (int i = 0; i < kSkillCount; ++i) {
        if (d.skills[i].points >= 0.0) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) { return d.skills[a].points < d.skills[b].points; });
    for (size_t k = 0; k < order.size() && k < 2; ++k) {
        const Skill sk = static_cast<Skill>(order[k]);
        const SkillPool& pool = PoolOf(sk);
        // Modes of that skill, weakest first (untested ones count as weak).
        std::vector<std::pair<double, ModeId>> modes;
        for (ModeId m : pool.modes) {
            AimRank r;
            const double p = ModeRank(stats, m, r) ? r.points : 3.0 + rng.Uniform(0.0, 1.0);
            modes.push_back({p, m});
        }
        std::sort(modes.begin(), modes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        int tasks = k == 0 ? 2 : 1;
        for (size_t j = 0; tasks > 0 && j < modes.size(); ++j) {
            const ModeId m = modes[j].second;
            if (has(m)) continue;
            --tasks;
            AimRank r;
            const std::string where = ModeRank(stats, m, r) ? " (" + RankLabel(r) + " here)" : "";
            add(m, std::string(k == 0 ? "Weakest skill" : "Second weakest") + ": " + SkillName(sk) + ". " + ModeName(m) +
                       where + ".");
        }
    }

    // 4) Habits.
    if (d.overshootShare > 0.6) {
        add(ModeId::Microflex, "You overshoot: small, controlled corrections. Stop ON the target.");
    } else if (d.overshootShare >= 0.0 && d.overshootShare < 0.4) {
        add(ModeId::Spidershot, "You undershoot: commit to the whole flick, then fine-tune.");
    }
    if (d.clickAccuracy >= 0.0 && d.clickAccuracy < 0.65) {
        PlanStep s{ModeId::LongRange, Difficulty::Easy, 45, "Low accuracy: slow down and land every click."};
        if (!has(s.mode) && plan.size() < 7) plan.push_back(s);
    }

    // 5) Finish by re-testing the weakest skill, to see the change.
    if (!order.empty()) {
        const Skill sk = static_cast<Skill>(order[0]);
        const ModeId m = PoolOf(sk).modes.front();
        if (!has(m)) add(m, std::string("Check: how is your ") + SkillName(sk) + " now?");
    }
    // Enough data but everything is strong: keep it varied.
    for (int tries = 0; plan.size() < 5 && tries < 50; ++tries) {
        const SkillPool& pool = Pools()[static_cast<size_t>(rng.Int(0, kSkillCount - 1))];
        add(pool.modes[static_cast<size_t>(rng.Int(0, static_cast<int>(pool.modes.size()) - 1))],
            "Keep it varied: all-round practice.");
    }
    return plan;
}
