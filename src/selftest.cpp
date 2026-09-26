// selftest.cpp - known-answer tests for the math the trainer relies on.
#include "selftest.h"

#include <cmath>
#include <cstdio>

#include "camera.h"
#include "crosshair.h"
#include "sens_finder.h"
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
