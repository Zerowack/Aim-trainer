// sens_finder.h - Perfect Sensitivity Approximation (PSA) finder.
//
// PSA narrows a sensitivity range by repeated A/B comparison:
//   start: low = sens x 0.5, high = sens x 1.5
//   each round: play one test at 'low' and one at 'high' (random order,
//   labelled A/B so you don't know which is which). The worse side is moved
//   to the midpoint of the range, so the range halves toward the better side.
//   After 7 rounds the range is 1/128 of the start width and its midpoint is
//   the recommendation.
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
    double comfort = 0.0;    // from the 1-5 rating
    double total = 0.0;      // weighted, 0..100
};

FinderScore ScoreFinderTest(const RunStats& s, int comfort);

struct FinderTest {
    int round = 0;      // 1-based
    char label = 'A';   // A or B (shown to the player)
    double sens = 0.0;
    int comfort = 3;
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
};

class SensFinder {
public:
    static constexpr int kRounds = 7;
    static constexpr double kTestSeconds = 20.0;

    // Bit n of 'orderBits' decides whether round n tests the high sens first
    // (pass random bits so the order is unpredictable).
    void Start(double currentSens, double dpi, unsigned int orderBits);
    bool Active() const { return active_; }
    bool Finished() const { return finished_; }

    int Round() const { return round_; }            // 1..kRounds
    int TestInRound() const { return testInRound_; }  // 0 or 1
    char CurrentLabel() const { return testInRound_ == 0 ? 'A' : 'B'; }
    double CurrentSens() const;
    double Low() const { return low_; }
    double High() const { return high_; }
    double StartSens() const { return startSens_; }
    double Dpi() const { return dpi_; }

    // Records the finished test. Returns true if this completed a round.
    bool Submit(const RunStats& stats, int comfort);
    // Result of the last completed round.
    bool LastRoundHighWon() const { return lastHighWon_; }
    const FinderTest* LastRoundTest(bool high) const;

    double Recommended() const { return 0.5 * (low_ + high_); }
    const std::vector<FinderTest>& Tests() const { return tests_; }

    // Persists the finished session (sessions + per-test CSV files).
    bool Save(const std::string& dir, FinderSession* outSession) const;
    void Cancel() { active_ = false; }

private:
    bool IsHighFirst(int round) const { return ((orderBits_ >> round) & 1u) != 0; }

    bool active_ = false;
    bool finished_ = false;
    double startSens_ = 0.4;
    double dpi_ = 800.0;
    double low_ = 0.2;
    double high_ = 0.6;
    int round_ = 1;
    int testInRound_ = 0;
    unsigned int orderBits_ = 0;
    bool lastHighWon_ = false;
    std::vector<FinderTest> tests_;
    std::string sessionId_;
    std::string timestamp_;
};

std::vector<FinderSession> LoadFinderSessions(const std::string& dir);
// Average of all sessions, done in cm/360 so sessions at different DPIs can
// be combined, then converted back to a sens at 'currentDpi'. Returns 0 if
// there are no sessions.
double CombinedRecommendation(const std::vector<FinderSession>& sessions, double currentDpi);
