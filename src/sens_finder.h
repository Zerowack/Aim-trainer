// sens_finder.h - Performance-based sensitivity finder.
//
// No questions, no feel ratings: the sens is picked from how you actually
// perform. Every test is the same 20 second mix (flicks, tracking, micro-
// adjustments) at a hidden sens:
//   1. Warm-up: one test at your current sens (not counted).
//   2. Scan: 7 sensitivities spread evenly (on a log scale) from x0.55 to
//      x1.8 of the pro average (280 eDPI = 0.175 at 1600 DPI, converted to
//      your DPI), widened to include your own sens if it lies outside, in
//      random order.
//   3. Refine: 5 sensitivities from x0.67 to x1.5 of the scan's best
//      estimate, each played twice, in random order.
// The recommendation is the peak of a curve fitted through every scored
// test (score vs log sens, a locally weighted parabola), with a term for
// improving over the session so warming up doesn't bias it towards the
// sens you played last. Tuned in simulation: with a clear performance
// difference between sensitivities it lands within ~3-6% of the true best.
#pragma once

#include <string>
#include <vector>

#include "stats.h"

// Score breakdown for one 20 second test (each component 0..1).
struct FinderScore {
    double accuracy = 0.0;
    double speed = 0.0;      // from average time-to-kill
    double precision = 0.0;  // from click error relative to target size
    double tracking = 0.0;   // time on target %
    double total = 0.0;      // weighted, 0..100
};

FinderScore ScoreFinderTest(const RunStats& s);

enum class FinderPhase { Warmup, Scan, Refine };
const char* FinderPhaseName(FinderPhase p);

struct FinderTest {
    int index = 0;  // 1-based, in play order
    FinderPhase phase = FinderPhase::Scan;
    double sens = 0.0;
    FinderScore score;
    double accuracyPct = 0.0;
    double avgTtkMs = -1.0;
    double trackingPct = 0.0;
    double overshootPct = -1.0;
};

struct FinderSession {
    std::string id;
    std::string timestamp;
    double dpi = 0.0;
    double startSens = 0.0;
    double recommended = 0.0;
    double cm360 = 0.0;
    // Did the session find a real peak? A result stuck at the edge of the
    // tested range is only a direction, not an answer. Sessions saved before
    // this flag existed are judged by whether the result sits on a range edge.
    bool reliable = true;
};

// Result of the curve fit.
struct FinderFit {
    bool valid = false;       // enough tests to fit
    bool peaked = false;      // the curve has a real maximum inside the tested range
    double best = 0.0;        // recommended sens
    double low = 0.0;         // likely range (about +-1 standard error)
    double high = 0.0;
    double a = 0.0, b = 0.0, c = 0.0;  // score = a + b x + c x^2, x = ln(sens / pro sens), at the session's end
};

class SensFinder {
public:
    static constexpr int kScanTests = 7;
    static constexpr int kRefineSens = 5;
    static constexpr int kRefineRepeats = 2;
    static constexpr int kTotalTests = 1 + kScanTests + kRefineSens * kRefineRepeats;
    static constexpr double kTestSeconds = 20.0;
    static constexpr double kProEdpi = 280.0;  // pro average: 0.175 at 1600 DPI

    // The pro-average sens at a given DPI (the scan's centre).
    static double ProSens(double dpi) { return dpi > 0.0 ? kProEdpi / dpi : 0.35; }
    // Scan range for a session: x0.55 .. x1.8 of the pro sens, widened so it
    // also covers x0.8 .. x1.25 of your own sens.
    static void ScanRange(double userSens, double dpi, double& lo, double& hi);

    void Start(double userSens, double dpi, unsigned int seed);
    bool Active() const { return active_; }
    bool Finished() const { return finished_; }

    int TestNumber() const { return static_cast<int>(tests_.size()) + 1; }  // 1..kTotalTests
    FinderPhase CurrentPhase() const;
    double CurrentSens() const;
    double StartSens() const { return startSens_; }  // your sens when the session started
    double BaseSens() const { return baseSens_; }    // pro average at your DPI
    double Dpi() const { return dpi_; }

    // Records the finished test and moves on.
    void Submit(const RunStats& stats);

    const FinderFit& Fit() const { return fit_; }
    double Recommended() const { return fit_.valid ? fit_.best : startSens_; }
    const std::vector<FinderTest>& Tests() const { return tests_; }

    // Persists the finished session (sessions + per-test CSV files).
    bool Save(const std::string& dir, FinderSession* outSession) const;
    void Cancel() { active_ = false; }

private:
    void Shuffle(std::vector<double>& v);
    void PlanRefine();
    FinderFit FitTests() const;

    bool active_ = false;
    bool finished_ = false;
    double startSens_ = 0.4;
    double baseSens_ = 0.35;
    double dpi_ = 800.0;
    unsigned int rng_ = 1;
    std::vector<double> plan_;  // sens of every test, in play order
    std::vector<FinderTest> tests_;
    FinderFit fit_;
    std::string sessionId_;
    std::string timestamp_;
};

std::vector<FinderSession> LoadFinderSessions(const std::string& dir);
// Combined result of all saved sessions, done in cm/360 so sessions at
// different DPIs can be combined, then converted back to a sens at
// 'currentDpi'. It is the median of the reliable sessions (edge results are
// left out while at least one real peak exists), so one odd session can't
// drag it.
struct CombinedResult {
    double sens = 0.0;      // 0 = no sessions
    int used = 0;           // sessions in the median
    int leftOut = 0;        // edge results not counted
    double spreadPct = 0.0; // largest / smallest cm/360 of the used sessions, in % above 1
    bool Disagree() const { return used >= 2 && spreadPct > 30.0; }
};
CombinedResult CombineSessions(const std::vector<FinderSession>& sessions, double currentDpi);
double CombinedRecommendation(const std::vector<FinderSession>& sessions, double currentDpi);
