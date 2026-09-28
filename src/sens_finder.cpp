// sens_finder.cpp - Sens Finder test plan, curve fit, scoring and persistence.
#include "sens_finder.h"

#include <algorithm>
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

FinderScore ScoreFinderTest(const RunStats& s) {
    FinderScore f;
    f.accuracy = s.shots > 0 ? static_cast<double>(s.hits) / s.shots : 0.0;

    // 350 ms per kill or faster = full marks, 1.5 s or slower = zero.
    const double ttk = s.AvgTtkMs();
    f.speed = ttk > 0.0 ? Clamp01(1.0 - (ttk - 350.0) / 1150.0) : 0.0;

    // Average click error in "target radii": 0 = dead centre, 3+ = zero.
    const double err = s.MeanErrNorm();
    f.precision = err >= 0.0 ? Clamp01(1.0 - err / 3.0) : 0.0;

    f.tracking = s.trackTotalTime > 0.0 ? Clamp01(s.trackOnTime / s.trackTotalTime) : 0.0;

    f.total = 100.0 * (0.30 * f.accuracy + 0.25 * f.speed + 0.20 * f.precision + 0.25 * f.tracking);
    return f;
}

const char* FinderPhaseName(FinderPhase p) {
    switch (p) {
        case FinderPhase::Warmup: return "WARM-UP";
        case FinderPhase::Scan: return "SCAN";
        default: return "REFINE";
    }
}

namespace {

// Solves the n x n system m * x = v (Gaussian elimination, partial pivoting).
bool Solve(std::vector<std::vector<double>> m, std::vector<double> v, std::vector<double>& x) {
    const size_t n = v.size();
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; ++r) {
            if (std::fabs(m[r][col]) > std::fabs(m[piv][col])) piv = r;
        }
        if (std::fabs(m[piv][col]) < 1e-12) return false;
        std::swap(m[piv], m[col]);
        std::swap(v[piv], v[col]);
        for (size_t r = 0; r < n; ++r) {
            if (r == col) continue;
            const double k = m[r][col] / m[col][col];
            for (size_t c = col; c < n; ++c) m[r][c] -= k * m[col][c];
            v[r] -= k * v[col];
        }
    }
    x.resize(n);
    for (size_t i = 0; i < n; ++i) x[i] = v[i] / m[i][i];
    return true;
}

bool Invert(std::vector<std::vector<double>> m, std::vector<std::vector<double>>& inv) {
    const size_t n = m.size();
    inv.assign(n, std::vector<double>(n, 0.0));
    for (size_t i = 0; i < n; ++i) inv[i][i] = 1.0;
    for (size_t col = 0; col < n; ++col) {
        size_t piv = col;
        for (size_t r = col + 1; r < n; ++r) {
            if (std::fabs(m[r][col]) > std::fabs(m[piv][col])) piv = r;
        }
        if (std::fabs(m[piv][col]) < 1e-12) return false;
        std::swap(m[piv], m[col]);
        std::swap(inv[piv], inv[col]);
        const double d = m[col][col];
        for (size_t c = 0; c < n; ++c) {
            m[col][c] /= d;
            inv[col][c] /= d;
        }
        for (size_t r = 0; r < n; ++r) {
            if (r == col) continue;
            const double k = m[r][col];
            for (size_t c = 0; c < n; ++c) {
                m[r][c] -= k * m[col][c];
                inv[r][c] -= k * inv[col][c];
            }
        }
    }
    return true;
}

}  // namespace

void SensFinder::Start(double currentSens, double dpi, unsigned int seed) {
    active_ = true;
    finished_ = false;
    startSens_ = currentSens;
    dpi_ = dpi;
    rng_ = seed * 2654435761u + 12345u;
    if (rng_ == 0) rng_ = 1;
    tests_.clear();
    fit_ = FinderFit{};

    // Warm-up at the current sens, then the scan in random order.
    plan_.clear();
    plan_.push_back(currentSens);
    std::vector<double> scan;
    const double lo = std::log(0.55), hi = std::log(1.8);
    for (int i = 0; i < kScanTests; ++i) {
        scan.push_back(currentSens * std::exp(lo + (hi - lo) * i / (kScanTests - 1)));
    }
    Shuffle(scan);
    plan_.insert(plan_.end(), scan.begin(), scan.end());

    const std::time_t now = std::time(nullptr);
    sessionId_ = std::to_string(static_cast<long long>(now));
    timestamp_ = NowTimestamp();
}

void SensFinder::Shuffle(std::vector<double>& v) {
    for (size_t i = v.size(); i > 1; --i) {
        rng_ ^= rng_ << 13;
        rng_ ^= rng_ >> 17;
        rng_ ^= rng_ << 5;
        std::swap(v[i - 1], v[rng_ % i]);
    }
}

FinderPhase SensFinder::CurrentPhase() const {
    const size_t i = tests_.size();
    if (i == 0) return FinderPhase::Warmup;
    if (i <= static_cast<size_t>(kScanTests)) return FinderPhase::Scan;
    return FinderPhase::Refine;
}

double SensFinder::CurrentSens() const {
    const size_t i = tests_.size();
    return i < plan_.size() ? plan_[i] : Recommended();
}

void SensFinder::Submit(const RunStats& stats) {
    if (!active_ || finished_) return;
    FinderTest t;
    t.index = static_cast<int>(tests_.size()) + 1;
    t.phase = CurrentPhase();
    t.sens = CurrentSens();
    t.score = ScoreFinderTest(stats);
    t.accuracyPct = stats.shots > 0 ? 100.0 * stats.hits / stats.shots : 0.0;
    t.avgTtkMs = stats.AvgTtkMs();
    t.trackingPct = stats.trackTotalTime > 0.0 ? 100.0 * stats.trackOnTime / stats.trackTotalTime : 0.0;
    const double share = stats.OvershootShare();
    t.overshootPct = share >= 0.0 ? share * 100.0 : -1.0;
    tests_.push_back(t);

    fit_ = FitTests();
    if (tests_.size() == static_cast<size_t>(1 + kScanTests)) PlanRefine();
    if (tests_.size() >= static_cast<size_t>(kTotalTests)) finished_ = true;
}

// Refine around the scan's estimate: x0.67 .. x1.5, each twice, shuffled.
void SensFinder::PlanRefine() {
    double centre = fit_.valid ? fit_.best : startSens_;
    centre = std::max(startSens_ * 0.55, std::min(startSens_ * 1.8, centre));
    std::vector<double> refine;
    const double lo = std::log(1.0 / 1.5), hi = std::log(1.5);
    for (int r = 0; r < kRefineRepeats; ++r) {
        for (int i = 0; i < kRefineSens; ++i) refine.push_back(centre * std::exp(lo + (hi - lo) * i / (kRefineSens - 1)));
    }
    Shuffle(refine);
    plan_.resize(1 + kScanTests);
    plan_.insert(plan_.end(), refine.begin(), refine.end());
}

// Weighted least squares: score = a + b x + c x^2 (+ d t), x = ln(sens / start),
// t = test order 0..1 (learning / fatigue over the session). Performance
// flattens far from your best sens, so a parabola only fits near the peak:
// the fit is repeated with tests weighted by their distance to the current
// estimate (local regression), which keeps far-off tests from dragging it.
FinderFit SensFinder::FitTests() const {
    FinderFit f;
    std::vector<const FinderTest*> used;
    for (const FinderTest& t : tests_) {
        if (t.phase != FinderPhase::Warmup) used.push_back(&t);
    }
    const size_t n = used.size();
    if (n < 4) return f;
    const bool trend = n >= 9;  // enough tests to separate improvement from sens
    const size_t p = trend ? 4 : 3;
    std::vector<double> xs(n), ts(n), ys(n);
    double xmin = 1e9, xmax = -1e9;
    for (size_t k = 0; k < n; ++k) {
        xs[k] = std::log(used[k]->sens / startSens_);
        ts[k] = n > 1 ? static_cast<double>(k) / static_cast<double>(n - 1) : 0.0;
        ys[k] = used[k]->score.total;
        xmin = std::min(xmin, xs[k]);
        xmax = std::max(xmax, xs[k]);
    }
    auto row = [&](size_t k) {
        std::vector<double> r = {1.0, xs[k], xs[k] * xs[k]};
        if (trend) r.push_back(ts[k]);
        return r;
    };

    constexpr double kBandwidth = 0.5;  // ln units: weight 1/e at x0.61 / x1.65 of the estimate
    std::vector<double> beta;
    std::vector<std::vector<double>> xtx;
    std::vector<double> w(n, 1.0);
    double centre = 0.0;
    bool haveCentre = false;
    for (int iter = 0; iter < 5; ++iter) {
        double wsum = 0.0;
        for (size_t k = 0; k < n; ++k) {
            const double d = haveCentre ? (xs[k] - centre) / kBandwidth : 0.0;
            w[k] = std::exp(-d * d);
            wsum += w[k];
        }
        if (wsum < static_cast<double>(p) + 1.5) break;  // too few nearby tests: keep the previous fit
        std::vector<std::vector<double>> m(p, std::vector<double>(p, 0.0));
        std::vector<double> v(p, 0.0);
        for (size_t k = 0; k < n; ++k) {
            const std::vector<double> r = row(k);
            for (size_t i = 0; i < p; ++i) {
                v[i] += w[k] * r[i] * ys[k];
                for (size_t j = 0; j < p; ++j) m[i][j] += w[k] * r[i] * r[j];
            }
        }
        std::vector<double> sol;
        if (!Solve(m, v, sol)) break;
        beta = sol;
        xtx = m;
        // Next centre: the peak if there is one inside the range, else the best tested point on the curve.
        if (beta[2] < -1e-9 && -beta[1] / (2.0 * beta[2]) >= xmin && -beta[1] / (2.0 * beta[2]) <= xmax) {
            centre = -beta[1] / (2.0 * beta[2]);
        } else {
            double bestS = -1e18;
            for (size_t k = 0; k < n; ++k) {
                const double sc = beta[0] + beta[1] * xs[k] + beta[2] * xs[k] * xs[k];
                if (sc > bestS) {
                    bestS = sc;
                    centre = xs[k];
                }
            }
        }
        haveCentre = true;
    }
    if (beta.empty()) return f;
    f.valid = true;
    // Curve as of the end of the session (fully warmed up).
    f.a = beta[0] + (trend ? beta[3] : 0.0);
    f.b = beta[1];
    f.c = beta[2];

    // Weighted residual variance -> standard error of the peak (delta method).
    double rss = 0.0, wsum = 0.0;
    for (size_t k = 0; k < n; ++k) {
        const std::vector<double> r = row(k);
        double pred = 0.0;
        for (size_t i = 0; i < p; ++i) pred += beta[i] * r[i];
        rss += w[k] * (ys[k] - pred) * (ys[k] - pred);
        wsum += w[k];
    }
    const double sigma2 = wsum > static_cast<double>(p) ? rss / (wsum - static_cast<double>(p)) : rss;

    if (f.c < -1e-9) {
        const double xp = -f.b / (2.0 * f.c);
        if (xp >= xmin && xp <= xmax) {
            f.peaked = true;
            f.best = startSens_ * std::exp(xp);
            std::vector<std::vector<double>> inv;
            double se = 0.15;
            if (Invert(xtx, inv)) {
                const double gb = -1.0 / (2.0 * f.c), gc = f.b / (2.0 * f.c * f.c);
                const double var = sigma2 * (gb * gb * inv[1][1] + 2.0 * gb * gc * inv[1][2] + gc * gc * inv[2][2]);
                se = std::sqrt(std::max(0.0, var));
            }
            se = std::min(se, 0.4);
            f.low = f.best * std::exp(-se);
            f.high = f.best * std::exp(se);
            return f;
        }
    }
    // No peak inside the range: the best tested sens on the fitted curve
    // (a rising curve points past the edge; the next session starts there).
    double bestScore = -1e18;
    for (size_t k = 0; k < n; ++k) {
        const double sc = f.a + f.b * xs[k] + f.c * xs[k] * xs[k];
        if (sc > bestScore) {
            bestScore = sc;
            f.best = used[k]->sens;
        }
    }
    f.low = f.best * std::exp(-0.2);
    f.high = f.best * std::exp(0.2);
    return f;
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
    const std::string testsPath = dir + "finder_tests_v2.csv";

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
        to << "session_id,test,phase,sens,score,accuracy_pct,avg_ttk_ms,precision,tracking_pct,overshoot_pct\n";
    }
    for (const FinderTest& t : tests_) {
        to << s.id << ',' << t.index << ',' << FinderPhaseName(t.phase) << ',' << Fmt(t.sens, 4) << ',' << Fmt(t.score.total, 2)
           << ',' << Fmt(t.accuracyPct, 1) << ',' << Fmt(t.avgTtkMs, 1) << ',' << Fmt(t.score.precision, 3) << ','
           << Fmt(t.trackingPct, 1) << ',' << Fmt(t.overshootPct, 1) << "\n";
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
