// sens_finder.cpp - PSA state machine, scoring and persistence.
#include "sens_finder.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include "camera.h"

namespace {

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

std::string Fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::vector<std::string> Split(const std::string& line) {
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

bool FileIsEmpty(const std::string& path) {
    std::ifstream in(path);
    return !in || in.peek() == std::ifstream::traits_type::eof();
}

}  // namespace

FinderScore ScoreFinderTest(const RunStats& s, int comfort) {
    FinderScore f;
    f.accuracy = s.shots > 0 ? static_cast<double>(s.hits) / s.shots : 0.0;

    // 350 ms per kill or faster = full marks, 1.5 s or slower = zero.
    const double ttk = s.AvgTtkMs();
    f.speed = ttk > 0.0 ? Clamp01(1.0 - (ttk - 350.0) / 1150.0) : 0.0;

    // Average click error in "target radii": 0 = dead centre, 3+ = zero.
    const double err = s.MeanErrNorm();
    f.precision = err >= 0.0 ? Clamp01(1.0 - err / 3.0) : 0.0;

    f.tracking = s.trackTotalTime > 0.0 ? Clamp01(s.trackOnTime / s.trackTotalTime) : 0.0;
    f.comfort = Clamp01((comfort - 1) / 4.0);

    f.total = 100.0 * (0.25 * f.accuracy + 0.20 * f.speed + 0.15 * f.precision + 0.25 * f.tracking + 0.15 * f.comfort);
    return f;
}

void SensFinder::Start(double currentSens, double dpi, unsigned int orderBits) {
    active_ = true;
    finished_ = false;
    startSens_ = currentSens;
    dpi_ = dpi;
    low_ = currentSens * 0.5;
    high_ = currentSens * 1.5;
    round_ = 1;
    testInRound_ = 0;
    orderBits_ = orderBits;
    tests_.clear();

    const std::time_t now = std::time(nullptr);
    sessionId_ = std::to_string(static_cast<long long>(now));
    timestamp_ = NowTimestamp();
}

double SensFinder::CurrentSens() const {
    const bool highFirst = IsHighFirst(round_);
    const bool testingHigh = (testInRound_ == 0) ? highFirst : !highFirst;
    return testingHigh ? high_ : low_;
}

bool SensFinder::Submit(const RunStats& stats, int comfort) {
    if (!active_ || finished_) return false;
    FinderTest t;
    t.round = round_;
    t.label = CurrentLabel();
    t.sens = CurrentSens();
    t.comfort = comfort;
    t.score = ScoreFinderTest(stats, comfort);
    t.accuracyPct = stats.shots > 0 ? 100.0 * stats.hits / stats.shots : 0.0;
    t.avgTtkMs = stats.AvgTtkMs();
    t.trackingPct = stats.trackTotalTime > 0.0 ? 100.0 * stats.trackOnTime / stats.trackTotalTime : 0.0;
    const double share = stats.OvershootShare();
    t.overshootPct = share >= 0.0 ? share * 100.0 : -1.0;
    tests_.push_back(t);

    if (testInRound_ == 0) {
        testInRound_ = 1;
        return false;
    }

    // Both sides tested: narrow toward the winner.
    const FinderTest* hi = LastRoundTest(true);
    const FinderTest* lo = LastRoundTest(false);
    lastHighWon_ = hi && lo && hi->score.total >= lo->score.total;
    const double mid = 0.5 * (low_ + high_);
    if (lastHighWon_) low_ = mid;
    else high_ = mid;

    testInRound_ = 0;
    if (round_ >= kRounds) {
        finished_ = true;
    } else {
        ++round_;
    }
    return true;
}

const FinderTest* SensFinder::LastRoundTest(bool high) const {
    // The last two entries belong to the most recently completed (or current) round.
    if (tests_.size() < 2) return nullptr;
    const FinderTest& a = tests_[tests_.size() - 2];
    const FinderTest& b = tests_[tests_.size() - 1];
    if (a.round != b.round) return nullptr;
    const bool aIsHigher = a.sens > b.sens;
    if (high) return aIsHigher ? &a : &b;
    return aIsHigher ? &b : &a;
}

bool SensFinder::Save(const std::string& dir, FinderSession* outSession) const {
    FinderSession s;
    s.id = sessionId_;
    s.timestamp = timestamp_;
    s.dpi = dpi_;
    s.startSens = startSens_;
    s.recommended = Recommended();
    s.cm360 = val::Cm360(dpi_, s.recommended);
    if (outSession) *outSession = s;

    const std::string sessionsPath = dir + "finder_sessions.csv";
    const std::string testsPath = dir + "finder_tests.csv";

    const bool sessionsHeader = FileIsEmpty(sessionsPath);
    std::ofstream so(sessionsPath, std::ios::app);
    if (!so) return false;
    if (sessionsHeader) so << "session_id,timestamp,dpi,start_sens,recommended_sens,cm360,edpi\n";
    so << s.id << ',' << s.timestamp << ',' << Fmt(s.dpi, 0) << ',' << Fmt(s.startSens, 4) << ','
       << Fmt(s.recommended, 4) << ',' << Fmt(s.cm360, 2) << ',' << Fmt(val::Edpi(s.dpi, s.recommended), 1) << "\n";

    const bool testsHeader = FileIsEmpty(testsPath);
    std::ofstream to(testsPath, std::ios::app);
    if (!to) return false;
    if (testsHeader) {
        to << "session_id,round,label,sens,score,accuracy_pct,avg_ttk_ms,precision,tracking_pct,overshoot_pct,comfort\n";
    }
    for (const FinderTest& t : tests_) {
        to << s.id << ',' << t.round << ',' << t.label << ',' << Fmt(t.sens, 4) << ',' << Fmt(t.score.total, 2) << ','
           << Fmt(t.accuracyPct, 1) << ',' << Fmt(t.avgTtkMs, 1) << ',' << Fmt(t.score.precision, 3) << ','
           << Fmt(t.trackingPct, 1) << ',' << Fmt(t.overshootPct, 1) << ',' << t.comfort << "\n";
    }
    return static_cast<bool>(so) && static_cast<bool>(to);
}

std::vector<FinderSession> LoadFinderSessions(const std::string& dir) {
    std::vector<FinderSession> out;
    std::ifstream in(dir + "finder_sessions.csv");
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("session_id", 0) == 0) continue;
        const std::vector<std::string> f = Split(line);
        if (f.size() < 6) continue;
        FinderSession s;
        s.id = f[0];
        s.timestamp = f[1];
        s.dpi = std::strtod(f[2].c_str(), nullptr);
        s.startSens = std::strtod(f[3].c_str(), nullptr);
        s.recommended = std::strtod(f[4].c_str(), nullptr);
        s.cm360 = std::strtod(f[5].c_str(), nullptr);
        if (s.dpi > 0.0 && s.recommended > 0.0) out.push_back(s);
    }
    return out;
}

double CombinedRecommendation(const std::vector<FinderSession>& sessions, double currentDpi) {
    if (sessions.empty() || currentDpi <= 0.0) return 0.0;
    double sumCm = 0.0;
    for (const FinderSession& s : sessions) sumCm += val::Cm360(s.dpi, s.recommended);
    const double avgCm = sumCm / static_cast<double>(sessions.size());
    return val::SensFromCm360(currentDpi, avgCm);
}
