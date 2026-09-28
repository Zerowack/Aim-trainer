// selftest.cpp - known-answer tests for the math the trainer relies on.
#include "selftest.h"

#include <cmath>
#include <cstdio>

#include "camera.h"
#include "crosshair.h"
#include "sens_finder.h"
#include "rank.h"
#include "stats.h"
#include "training.h"

namespace {

struct Checker {
    std::string& report;
    int failed = 0;
    int passed = 0;

    void Near(const char* name, double got, double expected, double tol) {
        const bool ok = std::fabs(got - expected) <= tol;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "[%s] %s: got %.6f, expected %.6f (+/- %g)\n", ok ? "PASS" : "FAIL", name, got,
                      expected, tol);
        report += buf;
        if (ok) ++passed;
        else ++failed;
    }

    void True(const char* name, bool ok) {
        report += std::string(ok ? "[PASS] " : "[FAIL] ") + name + "\n";
        if (ok) ++passed;
        else ++failed;
    }
};

}  // namespace

bool RunSelfTest(std::string& report) {
    report.clear();
    Checker c{report};

    // --- Sensitivity math -------------------------------------------------
    c.Near("deg/count @ sens 1.0", val::DegreesPerCount(1.0), 0.07, 1e-12);
    c.Near("eDPI 800 x 0.4", val::Edpi(800.0, 0.4), 320.0, 1e-9);
    // 360 / (800 * 0.4 * 0.07) * 2.54 = 40.8214...
    c.Near("cm/360 @ 800 DPI, 0.4", val::Cm360(800.0, 0.4), 40.82142857, 1e-6);
    c.Near("cm/360 @ 1600 DPI, 0.2 (same eDPI)", val::Cm360(1600.0, 0.2), 40.82142857, 1e-6);
    c.Near("cm/360 @ 400 DPI, 1.0", val::Cm360(400.0, 1.0), 32.65714286, 1e-6);
    c.Near("sens from cm/360 round trip", val::SensFromCm360(800.0, val::Cm360(800.0, 0.37)), 0.37, 1e-9);

    // --- Camera: counts -> degrees, no smoothing, pitch clamp --------------
    ValCamera cam;
    cam.Reset();
    cam.ApplyCounts(1000, 0, 1.0);
    c.Near("1000 counts @ sens 1.0 -> 70 deg yaw", cam.Yaw(), 70.0, 1e-9);
    cam.Reset();
    for (int i = 0; i < 1000; ++i) cam.ApplyCounts(1, 0, 0.5);  // many tiny packets
    c.Near("1000 x 1 count @ 0.5 -> 35 deg (no loss)", cam.Yaw(), 35.0, 1e-9);
    cam.Reset();
    cam.ApplyCounts(0, -200, 0.5);  // mouse pushed away = look up
    c.Near("200 counts up @ 0.5 -> +7 deg pitch", cam.Pitch(), 7.0, 1e-9);
    cam.ApplyCounts(0, -100000, 1.0);
    c.Near("pitch clamped to +89", cam.Pitch(), 89.0, 1e-12);
    cam.ApplyCounts(0, 1000000, 1.0);
    c.Near("pitch clamped to -89", cam.Pitch(), -89.0, 1e-12);
    // 1000 counts at sens 10/70 is exactly 10 degrees; 36 of them make a full turn.
    cam.Reset();
    for (int i = 0; i < 36; ++i) cam.ApplyCounts(1000, 0, 10.0 / 70.0);
    c.Near("36 x 10 deg returns to 0 yaw", std::fabs(val::NormalizeDeg(cam.Yaw())), 0.0, 1e-6);

    // --- Aim history (used for overshoot and reaction onset) ---------------
    {
        AimHistory h;
        double t = 0.0;
        // 0.5 s of 8000 Hz packets turning 80 deg/s.
        for (int i = 0; i < 4000; ++i) {
            t += 1.0 / 8000.0;
            h.Push(t, static_cast<double>(i) * 0.01, 0.0);
        }
        double dy = 0.0, dp = 0.0;
        const bool ok = h.DeltaOver(t, 0.08, dy, dp);
        c.True("aim history has data at 8 kHz", ok);
        c.Near("aim history delta over 80 ms", dy, 6.41, 0.02);
        c.Near("aim history speed", h.Speed(t, 0.02), 80.0, 1.0);
        c.True("aim history is empty after 1 s idle", !h.DeltaOver(t + 1.0, 0.08, dy, dp));
    }

    // --- FOV (Hor+, 103 deg at 16:9) --------------------------------------
    c.Near("vertical FOV (fixed)", val::GameVerticalFov(), 70.5328004, 1e-5);
    c.Near("horizontal FOV @ 16:9", val::GameHorizontalFov(16.0 / 9.0), 103.0, 1e-9);
    c.Near("horizontal FOV @ 16:10", val::GameHorizontalFov(16.0 / 10.0), 97.0583640, 1e-5);
    c.Near("horizontal FOV @ 4:3", val::GameHorizontalFov(4.0 / 3.0), 86.6319709, 1e-5);
    c.Near("horizontal FOV @ 21:9", val::GameHorizontalFov(21.0 / 9.0), 117.5643840, 1e-5);

    // --- Scoped sensitivity (Valorant: sens x multiplier / zoom) -------------
    c.Near("scoped sens 0.4 @ 2.5x, multiplier 1.0", val::ScopedSens(0.4, 1.0, 2.5), 0.16, 1e-12);
    c.Near("scoped sens 0.4 @ 5x, multiplier 0.8", val::ScopedSens(0.4, 0.8, 5.0), 0.064, 1e-12);
    c.Near("unscoped sens unchanged", val::ScopedSens(0.4, 0.5, 1.0), 0.4, 1e-12);
    // 2 x atan(tan(70.53/2) / 2.5) = 31.589 degrees
    c.Near("vertical FOV at 2.5x zoom", val::ZoomedVerticalFov(2.5), 31.5886, 1e-3);
    c.Near("zoom 1.0 keeps the normal FOV", val::ZoomedVerticalFov(1.0), val::GameVerticalFov(), 1e-12);

    // --- Direction helpers -------------------------------------------------
    double yaw = 0.0, pitch = 0.0;
    AnglesFromDirection(DirectionFromAngles(-123.0, 31.0), yaw, pitch);
    c.Near("direction round trip yaw", yaw, -123.0, 1e-4);
    c.Near("direction round trip pitch", pitch, 31.0, 1e-4);
    const Vector3 fwd = DirectionFromAngles(0.0, 0.0);
    c.True("yaw 0 looks down -Z", std::fabs(fwd.z + 1.0f) < 1e-6f);
    const Vector3 right = DirectionFromAngles(90.0, 0.0);
    c.True("yaw +90 looks toward +X (right)", std::fabs(right.x - 1.0f) < 1e-6f);

    // --- Sens finder with a "player" whose performance peaks at 0.33 --------
    {
        SensFinder f;
        const double best = 0.33;
        f.Start(0.4, 800.0, 0x5Au);
        int k = 0;
        while (!f.Finished()) {
            const double s = f.CurrentSens();
            const double x = std::log(s / best);
            RunStats rs;
            rs.mode = ModeId::Mixed;
            rs.shots = 1000;
            // Peaked performance plus a learning trend and a little noise.
            rs.hits = static_cast<int>(std::lround(850.0 - 900.0 * x * x + 3.0 * k + ((k * 37) % 7 - 3) * 2.0));
            f.Submit(rs);
            ++k;
        }
        c.True("sens finder runs warm-up + scan + refine", static_cast<int>(f.Tests().size()) == SensFinder::kTotalTests);
        c.True("sens finder finds a peak", f.Fit().peaked);
        c.Near("sens finder converges near the best sens", f.Recommended(), best, 0.012);
        bool refineAround = true;
        for (const FinderTest& t : f.Tests()) {
            if (t.phase == FinderPhase::Refine) refineAround = refineAround && std::fabs(std::log(t.sens / best)) < 0.5;  // x0.67-x1.5 of the estimate
        }
        c.True("sens finder refines around the best area", refineAround);
    }
    {
        c.Near("pro base sens at 1600 DPI", SensFinder::ProSens(1600.0), 0.175, 1e-9);
        c.Near("pro base sens at 800 DPI", SensFinder::ProSens(800.0), 0.35, 1e-9);
        double lo = 0.0, hi = 0.0;
        SensFinder::ScanRange(0.175, 1600.0, lo, hi);
        c.True("scan covers x0.55-x1.8 of the pro sens", std::fabs(lo - 0.175 * 0.55) < 1e-9 && std::fabs(hi - 0.175 * 1.8) < 1e-9);
        SensFinder::ScanRange(1.0, 800.0, lo, hi);  // 800 eDPI: far above the pro range
        c.True("scan widens to include a high own sens", hi >= 1.25 - 1e-9 && lo <= 0.35 * 0.55 + 1e-9);
    }

    // --- Combined recommendation across DPIs --------------------------------
    {
        std::vector<FinderSession> ss(2);
        ss[0].dpi = 800.0;
        ss[0].recommended = 0.4;
        ss[1].dpi = 1600.0;
        ss[1].recommended = 0.2;
        c.Near("combined recommendation (same cm/360)", CombinedRecommendation(ss, 800.0), 0.4, 1e-9);

        // A real result (0.249) and an edge result (0.096 = pro x0.55 at 1600 DPI):
        // the edge one is left out instead of dragging the average to 0.139.
        std::vector<FinderSession> mixed(2);
        mixed[0].dpi = 1600.0;
        mixed[0].startSens = 0.23;
        mixed[0].recommended = 0.249;
        mixed[1].dpi = 1600.0;
        mixed[1].startSens = 0.23;
        mixed[1].recommended = 0.09625;
        mixed[1].reliable = false;
        const CombinedResult cr = CombineSessions(mixed, 1600.0);
        c.True("combined leaves edge results out", cr.used == 1 && cr.leftOut == 1 && std::fabs(cr.sens - 0.249) < 1e-9);
        std::vector<FinderSession> three(3);
        const double sens3[3] = {0.20, 0.22, 0.40};
        for (int i = 0; i < 3; ++i) {
            three[static_cast<size_t>(i)].dpi = 1600.0;
            three[static_cast<size_t>(i)].recommended = sens3[i];
        }
        const CombinedResult c3 = CombineSessions(three, 1600.0);
        c.True("combined uses the median and flags disagreement", std::fabs(c3.sens - 0.22) < 1e-9 && c3.Disagree());
    }

    // --- Coach sens suggestion (also used by auto-adjust) -------------------
    {
        RunStats over;
        over.overshoots = 14;
        over.undershoots = 4;
        over.relErrorSum = 0.06 * 18;
        over.relErrorCount = 18;
        c.True("coach: overshooting 6% -> lower sens 6%", SuggestedSensChangePct(over) == -6);
        RunStats under = over;
        under.overshoots = 3;
        under.undershoots = 15;
        under.relErrorSum = -0.25 * 18;  // clamps to the 15% maximum
        c.True("coach: undershooting -> raise sens (max 15%)", SuggestedSensChangePct(under) == 15);
        RunStats balanced = over;
        balanced.overshoots = 9;
        balanced.undershoots = 9;
        c.True("coach: balanced -> no change", SuggestedSensChangePct(balanced) == 0);
        RunStats few = over;
        few.overshoots = 3;
        few.undershoots = 1;
        c.True("coach: too little data -> no change", SuggestedSensChangePct(few) == 0);
    }

    // --- Aim rank estimate ---------------------------------------------------
    {
        c.True("rank: 0 points = Iron 1", RankLabel(RankFromPoints(0.0)) == "Iron 1");
        c.True("rank: 3 points = Bronze 1", RankLabel(RankFromPoints(3.0)) == "Bronze 1");
        c.True("rank: 10.5 points = Gold 2", RankLabel(RankFromPoints(10.5)) == "Gold 2");
        c.True("rank: 23.9 points = Immortal 3", RankLabel(RankFromPoints(23.9)) == "Immortal 3");
        c.True("rank: 24 points = Radiant", RankLabel(RankFromPoints(24.0)) == "Radiant");
        RunRecord g;
        g.mode = ModeId::Gridshot;
        g.duration = 60.0;
        g.hits = 153;  // 2.55 kills/s at 100% = exactly Gold's start
        g.accuracy = 100.0;
        AimRank r;
        c.True("rank: gridshot 2.55 kills/s = Gold 1", RankFromRecord(g, r) && RankLabel(r) == "Gold 1");
        g.accuracy = 50.0;  // same kills at half the accuracy = half the value
        c.True("rank: accuracy matters (50% acc drops to Iron)", RankFromRecord(g, r) && r.tier == 0);
        g.hits = 176;  // 176 hits, 91.2% -> 2.68 = Gold 2
        g.accuracy = 91.2;
        c.True("rank: 176 hits @ 91% in 60 s = Gold 2", RankFromRecord(g, r) && RankLabel(r) == "Gold 2");
        RunRecord rx;
        rx.mode = ModeId::Reaction;
        rx.duration = 60.0;
        rx.hits = 20;
        rx.avgReactionMs = 248.0;
        c.True("rank: reaction 248 ms = Gold 1", RankFromRecord(rx, r) && RankLabel(r) == "Gold 1");
        rx.avgReactionMs = 190.0;
        c.True("rank: reaction 190 ms = Radiant", RankFromRecord(rx, r) && RankLabel(r) == "Radiant");
        rx.avgReactionMs = 420.0;
        c.True("rank: reaction 420 ms = Iron", RankFromRecord(rx, r) && r.tier == 0);
        RunRecord hard = g;  // Gold 2 value at Normal...
        hard.difficulty = Difficulty::Hard;  // ...x1.25 on Hard: 2.68 * 1.25 = 3.34 -> Diamond 2
        c.True("rank: Hard difficulty is worth more (Gold 2 -> Diamond 2)", RankFromRecord(hard, r) && RankLabel(r) == "Diamond 2");
        RunRecord easy = g;
        easy.difficulty = Difficulty::Easy;  // x0.75 -> 2.01 = Bronze 1
        c.True("rank: Easy difficulty is worth less (Gold 2 -> Bronze 1)", RankFromRecord(easy, r) && RankLabel(r) == "Bronze 1");
        RunRecord pl;
        pl.mode = ModeId::Placement;
        pl.duration = 60.0;
        pl.hits = 20;
        pl.placementErr = 3.5;  // Gold's start
        c.True("rank: placement 3.5 deg = Gold 1", RankFromRecord(pl, r) && RankLabel(r) == "Gold 1");
        pl.placementErr = 0.8;
        c.True("rank: placement 0.8 deg = Radiant", RankFromRecord(pl, r) && RankLabel(r) == "Radiant");
        RunRecord sn;
        sn.mode = ModeId::Sniper;
        sn.duration = 60.0;
        sn.peeks = 18;
        sn.kills = 18;
        sn.deaths = 0;
        sn.accuracy = 95.0;
        sn.avgTtkMs = 300.0;  // every peek killed, fast: 1.0 x 0.95 x 1.0
        c.True("rank: sniper 18/18 peeks at 300 ms = Radiant", RankFromRecord(sn, r) && RankLabel(r) == "Radiant");
        sn.peeks = 12;
        sn.kills = 6;
        sn.deaths = 2;
        sn.accuracy = 60.0;
        sn.avgTtkMs = 700.0;  // (6 - 1) / 12 x 0.6 x 0.5625 = 0.14
        c.True("rank: sniper 6/12 with 2 deaths = Iron 3", RankFromRecord(sn, r) && RankLabel(r) == "Iron 3");
        sn.peeks = -1;  // a run from the old sniper mode
        c.True("rank: old sniper runs are not ranked", !RankFromRecord(sn, r));
        RunRecord shortRun = g;
        shortRun.duration = 10.0;
        c.True("rank: runs under 20 s are not ranked", !RankFromRecord(shortRun, r));
        RunRecord mixed = g;
        mixed.mode = ModeId::Mixed;
        c.True("rank: sens finder tests are not ranked", !RankFromRecord(mixed, r));
        bool allText = true;
        for (int t = 0; t < kTierCount; ++t) {
            AimRank tr;
            tr.tier = t;
            for (unsigned int seed = 0; seed < 8; ++seed) {
                const char* roast = RankRoast(tr, seed);
                allText = allText && roast && roast[0] != '\0';
            }
            allText = allText && RankDescription(tr)[0] != '\0';
        }
        c.True("rank: every tier has roasts and a description", allText);
    }

    // --- Crosshair share code round trip ------------------------------------
    {
        Crosshair x;
        x.r = 255; x.g = 0; x.b = 128;
        x.centerDot = true;
        x.innerLength = 9;
        x.innerOffset = 0;
        x.outerShow = true;
        Crosshair y;
        std::string err;
        const bool ok = DecodeCrosshair(EncodeCrosshair(x), y, &err);
        c.True("crosshair code round trip", ok && y.r == 255 && y.b == 128 && y.centerDot && y.innerLength == 9 &&
                                                y.innerOffset == 0 && y.outerShow);
        c.True("crosshair code rejects garbage", !DecodeCrosshair("hello world", y, &err));

        // Valorant share codes.
        Crosshair v;
        const bool vok = DecodeAnyCrosshair("0;P;c;5;h;0;f;0;0l;4;0o;2;0a;1;0f;0;1b;0", v, &err);
        c.True("valorant code: cyan, no outline, 4/2 inner, no outer",
               vok && v.r == 0 && v.g == 255 && v.b == 255 && !v.outline && v.innerLength == 4 && v.innerOffset == 2 &&
                   v.innerOpacity > 0.99f && !v.outerShow);
        const bool vok2 = DecodeAnyCrosshair("0;s;1;P;c;8;u;FF8000FF;d;1;z;3;0t;1;A;c;1;0l;10", v, &err);
        c.True("valorant code: custom colour, dot, ADS section ignored",
               vok2 && v.r == 255 && v.g == 128 && v.b == 0 && v.centerDot && v.dotThickness == 3 && v.innerThickness == 1 &&
                   v.innerLength == 6 && v.outerShow);
        const bool vok3 = DecodeAnyCrosshair("0;P;d;1;f;0;0t;4;0l;1;0o;0;0a;1;0f;0;1b;0", v, &err);
        c.True("valorant code: white dot + short thick inner lines at offset 0",
               vok3 && v.r == 255 && v.g == 255 && v.b == 255 && v.centerDot && v.innerThickness == 4 &&
                   v.innerLength == 1 && v.innerOffset == 0 && v.innerOpacity > 0.99f && !v.outerShow && v.outline);
        c.True("valorant code: firing error off for 0f;0, fade off for f;0", vok3 && !v.innerFiringError && !v.fadeWithFiring);
        const bool vok4 = DecodeAnyCrosshair("0", v, &err);
        c.True("valorant default: firing error on (inner + outer), outer movement error, fade",
               vok4 && v.innerFiringError && v.outerFiringError && v.outerMoveError && !v.innerMoveError && v.fadeWithFiring &&
                   v.outerShow && v.innerVertLength == 6);
        const bool vok5 = DecodeAnyCrosshair("0;P;0l;3;0g;1;0v;9;m;1;1b;0", v, &err);
        c.True("valorant code: separate vertical length + override offset",
               vok5 && v.innerLength == 3 && v.innerSeparateVert && v.innerVertLength == 9 && v.overrideFiringOffset);
        Crosshair w = v;
        Crosshair w2;
        c.True("crosshair code round trip keeps Valorant extras",
               DecodeCrosshair(EncodeCrosshair(w), w2, &err) && w2.innerSeparateVert && w2.innerVertLength == 9 &&
                   w2.overrideFiringOffset && w2.innerFiringError == w.innerFiringError && w2.fadeWithFiring == w.fadeWithFiring);
        c.True("valorant code rejects missing P section", !DecodeValorantCrosshair("0;A;c;1", v, &err));
    }

    {
        // Training plans.
        const std::vector<PlanStep> warm = BuildWarmup();
        int warmSec = 0;
        for (const PlanStep& st : warm) warmSec += st.seconds;
        c.True("warmup: 7 ranked-length tasks, about 4-6 minutes", warm.size() == 7 && warmSec >= 240 && warmSec <= 360);
        StatsStore empty;
        const Diagnosis d = Diagnose(empty);
        bool allUntested = true;
        for (const SkillScore& sk : d.skills) allUntested = allUntested && sk.points < 0.0;
        c.True("coach: no runs = every skill untested", allUntested && d.clickAccuracy < 0.0 && d.overshootShare < 0.0);
        Rng rng(7);
        const std::vector<PlanStep> plan = BuildImprovePlan(d, empty, rng);
        bool unique = true, longEnough = true;
        for (size_t i = 0; i < plan.size(); ++i) {
            longEnough = longEnough && plan[i].seconds >= static_cast<int>(kMinRankedSeconds);
            for (size_t j = i + 1; j < plan.size(); ++j) unique = unique && plan[i].mode != plan[j].mode;
        }
        c.True("coach: first plan tests skills, 5-7 unique ranked tasks", plan.size() >= 5 && plan.size() <= 7 && unique && longEnough);
    }

    char summary[96];
    std::snprintf(summary, sizeof(summary), "Self-test: %d passed, %d failed\n", c.passed, c.failed);
    report += summary;
    return c.failed == 0;
}
