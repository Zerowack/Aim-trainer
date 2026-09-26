// selftest.cpp - known-answer tests for the math the trainer relies on.
#include "selftest.h"

#include <cmath>
#include <cstdio>

#include "camera.h"
#include "crosshair.h"
#include "sens_finder.h"
#include "rank.h"
#include "stats.h"

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

    // --- Direction helpers -------------------------------------------------
    double yaw = 0.0, pitch = 0.0;
    AnglesFromDirection(DirectionFromAngles(-123.0, 31.0), yaw, pitch);
    c.Near("direction round trip yaw", yaw, -123.0, 1e-4);
    c.Near("direction round trip pitch", pitch, 31.0, 1e-4);
    const Vector3 fwd = DirectionFromAngles(0.0, 0.0);
    c.True("yaw 0 looks down -Z", std::fabs(fwd.z + 1.0f) < 1e-6f);
    const Vector3 right = DirectionFromAngles(90.0, 0.0);
    c.True("yaw +90 looks toward +X (right)", std::fabs(right.x - 1.0f) < 1e-6f);

    // --- PSA narrowing with a perfect "player" whose best sens is 0.33 -----
    {
        SensFinder f;
        const double best = 0.33;
        f.Start(0.4, 800.0, 0x5Au);
        while (!f.Finished()) {
            const double s = f.CurrentSens();
            RunStats rs;
            rs.mode = ModeId::Mixed;
            rs.shots = 100;
            rs.hits = static_cast<int>(std::lround(100.0 - std::fabs(s - best) * 200.0));
            f.Submit(rs, 3);
        }
        c.Near("PSA range width after 7 rounds", f.High() - f.Low(), 0.4 / 128.0, 1e-9);
        c.Near("PSA converges near the best sens", f.Recommended(), best, 0.004);
        c.True("PSA recorded 14 tests", f.Tests().size() == 14);
    }

    // --- Combined recommendation across DPIs --------------------------------
    {
        std::vector<FinderSession> ss(2);
        ss[0].dpi = 800.0;
        ss[0].recommended = 0.4;
        ss[1].dpi = 1600.0;
        ss[1].recommended = 0.2;
        c.Near("combined recommendation (same cm/360)", CombinedRecommendation(ss, 800.0), 0.4, 1e-9);
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
        g.hits = 180;  // 3.0 kills/s at 100% = exactly Gold's start
        g.accuracy = 100.0;
        AimRank r;
        c.True("rank: gridshot 3.0 kills/s = Gold 1", RankFromRecord(g, r) && RankLabel(r) == "Gold 1");
        g.accuracy = 50.0;  // same kills at half the accuracy = half the value
        c.True("rank: accuracy matters (50% acc drops to Iron)", RankFromRecord(g, r) && r.tier == 0);
        g.hits = 176;  // the reported run: 176 hits, 91.2% -> 2.68 = Silver 2
        g.accuracy = 91.2;
        c.True("rank: 176 hits @ 91% in 60 s = Silver 2", RankFromRecord(g, r) && RankLabel(r) == "Silver 2");
        RunRecord rx;
        rx.mode = ModeId::Reaction;
        rx.duration = 60.0;
        rx.hits = 20;
        rx.avgReactionMs = 258.0;
        c.True("rank: reaction 258 ms = Gold 1", RankFromRecord(rx, r) && RankLabel(r) == "Gold 1");
        rx.avgReactionMs = 190.0;
        c.True("rank: reaction 190 ms = Radiant", RankFromRecord(rx, r) && RankLabel(r) == "Radiant");
        rx.avgReactionMs = 420.0;
        c.True("rank: reaction 420 ms = Iron", RankFromRecord(rx, r) && r.tier == 0);
        RunRecord hard = g;  // Silver 2 value at Normal...
        hard.difficulty = Difficulty::Hard;  // ...x1.25 on Hard: 2.68 * 1.25 = 3.35 -> Gold 3
        c.True("rank: Hard difficulty is worth more (Silver 2 -> Gold 3)", RankFromRecord(hard, r) && RankLabel(r) == "Gold 3");
        RunRecord easy = g;
        easy.difficulty = Difficulty::Easy;  // x0.75 -> 2.01 = Bronze 1
        c.True("rank: Easy difficulty is worth less (Silver 2 -> Bronze 1)", RankFromRecord(easy, r) && RankLabel(r) == "Bronze 1");
        RunRecord pl;
        pl.mode = ModeId::Placement;
        pl.duration = 60.0;
        pl.hits = 20;
        pl.placementErr = 6.5;  // Gold's start
        c.True("rank: placement 6.5 deg = Gold 1", RankFromRecord(pl, r) && RankLabel(r) == "Gold 1");
        pl.placementErr = 1.0;
        c.True("rank: placement 1.0 deg = Radiant", RankFromRecord(pl, r) && RankLabel(r) == "Radiant");
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
        c.True("valorant code rejects missing P section", !DecodeValorantCrosshair("0;A;c;1", v, &err));
    }

    char summary[96];
    std::snprintf(summary, sizeof(summary), "Self-test: %d passed, %d failed\n", c.passed, c.failed);
    report += summary;
    return c.failed == 0;
}
