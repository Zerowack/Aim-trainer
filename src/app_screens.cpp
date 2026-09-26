// app_screens.cpp - menus, results, settings, stats and sens finder screens.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "crosshair.h"
#include "input.h"
#include "rank_badge.h"
#include "ui.h"

using namespace ui;

namespace {

constexpr float kContentWidth = 1500.0f;

float ContentX() { return (VW() - kContentWidth) * 0.5f; }

Color Lerp2(Color a, Color b, float k) {
    auto l = [k](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * k);
    };
    return Color{l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

std::string Fmt(double v, int decimals) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", decimals, v);
    return buf;
}

std::string MsOrDash(double ms) { return ms > 0.0 ? Fmt(ms, 0) + " ms" : "-"; }

bool ParseNumber(const std::string& s, double minV, double maxV, double& out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0') return false;
    if (v < minV || v > maxV) return false;
    out = v;
    return true;
}

// Small line-art icon for each training mode, centred on (cx, cy).
void DrawModeIcon(ModeId m, float cx, float cy, float s, Color c) {
    const float t = 3.0f;  // stroke width
    switch (m) {
        case ModeId::Gridshot:
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j) Circle(Vector2{cx + static_cast<float>(i - 1) * s * 0.36f, cy + static_cast<float>(j - 1) * s * 0.36f}, s * 0.05f, Alpha(c, 0.35f));
            Circle(Vector2{cx - s * 0.36f, cy - s * 0.36f}, s * 0.13f, c);
            Circle(Vector2{cx + s * 0.36f, cy}, s * 0.13f, c);
            Circle(Vector2{cx, cy + s * 0.36f}, s * 0.13f, c);
            break;
        case ModeId::Microshot:
            CircleLines(Vector2{cx, cy}, s * 0.36f, t, Alpha(c, 0.5f));
            Line(Vector2{cx - s * 0.5f, cy}, Vector2{cx - s * 0.2f, cy}, t, c);
            Line(Vector2{cx + s * 0.2f, cy}, Vector2{cx + s * 0.5f, cy}, t, c);
            Line(Vector2{cx, cy - s * 0.5f}, Vector2{cx, cy - s * 0.2f}, t, c);
            Line(Vector2{cx, cy + s * 0.2f}, Vector2{cx, cy + s * 0.5f}, t, c);
            Circle(Vector2{cx, cy}, s * 0.07f, c);
            break;
        case ModeId::Tracking:
            Circle(Vector2{cx, cy - s * 0.3f}, s * 0.1f, c);
            Fill(Rectangle{cx - s * 0.1f, cy - s * 0.17f, s * 0.2f, s * 0.5f}, c);
            Tri(Vector2{cx - s * 0.5f, cy}, Vector2{cx - s * 0.28f, cy - s * 0.13f}, Vector2{cx - s * 0.28f, cy + s * 0.13f}, Alpha(c, 0.7f));
            Tri(Vector2{cx + s * 0.5f, cy}, Vector2{cx + s * 0.28f, cy - s * 0.13f}, Vector2{cx + s * 0.28f, cy + s * 0.13f}, Alpha(c, 0.7f));
            break;
        case ModeId::Flick180: {
            // Half-circle arrow.
            const int n = 14;
            Vector2 prev = {cx - s * 0.38f, cy + s * 0.08f};
            for (int i = 1; i <= n; ++i) {
                const float a = 3.14159265f * static_cast<float>(i) / static_cast<float>(n);
                const Vector2 p = {cx - s * 0.38f * std::cos(a), cy + s * 0.08f - s * 0.38f * std::sin(a)};
                Line(prev, p, t, c);
                prev = p;
            }
            Tri(Vector2{prev.x, prev.y + s * 0.22f}, Vector2{prev.x - s * 0.13f, prev.y - s * 0.02f},
                Vector2{prev.x + s * 0.13f, prev.y - s * 0.02f}, c);
            Circle(Vector2{cx - s * 0.38f, cy + s * 0.08f}, s * 0.07f, Alpha(c, 0.6f));
            break;
        }
        case ModeId::Reaction:
            // Lightning bolt.
            Tri(Vector2{cx + s * 0.12f, cy - s * 0.5f}, Vector2{cx - s * 0.26f, cy + s * 0.06f}, Vector2{cx + s * 0.02f, cy + s * 0.06f}, c);
            Tri(Vector2{cx - s * 0.02f, cy - s * 0.06f}, Vector2{cx + s * 0.26f, cy - s * 0.06f}, Vector2{cx - s * 0.12f, cy + s * 0.5f}, c);
            break;
        case ModeId::Peek:
            // Agent half hidden behind a wall.
            Circle(Vector2{cx + s * 0.08f, cy - s * 0.2f}, s * 0.1f, c);
            Fill(Rectangle{cx - s * 0.02f, cy - s * 0.07f, s * 0.2f, s * 0.5f}, c);
            Fill(Rectangle{cx - s * 0.5f, cy - s * 0.35f, s * 0.46f, s * 0.8f}, Alpha(c, 0.35f));
            break;
        default:
            break;
    }
}

// Clickable card with a title, description, footer line and mode icon.
bool Card(Rectangle r, ModeId mode, const std::string& title, const std::string& desc, const std::string& footer, int index) {
    const bool hover = Hover(r);
    const float a = HoverAnim(r);
    Angled(r, Lerp2(theme::kPanel, theme::kPanel2, a), 16.0f);
    // Accent edge grows on hover.
    Fill(Rectangle{r.x, r.y, 5.0f + 3.0f * a, r.height - 16.0f}, Lerp2(theme::kAccentDim, theme::kAccent, a));
    Text(TextFormat("%02d", index), r.x + r.width - 24.0f, r.y + 16.0f, 20.0f, Alpha(theme::kTextDim, 0.5f), Align::Right);
    TextBold(title, r.x + 26.0f, r.y + 16.0f, 30.0f, theme::kText);
    TextBlock(desc, r.x + 26.0f, r.y + 58.0f, r.width - 150.0f, 18.0f, theme::kTextDim);
    Text(footer, r.x + 26.0f + 6.0f * a, r.y + r.height - 34.0f, 18.0f, Lerp2(theme::kText, theme::kAccent, a));
    DrawModeIcon(mode, r.x + r.width - 68.0f, r.y + r.height * 0.55f, 70.0f, Lerp2(theme::kTextDim, theme::kText, a));
    return hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Text colour-coded to the tier.
void RankText(const AimRank& r, float x, float y, float size, Align a = Align::Left) {
    TextBold(RankLabel(r), x, y, size, TierColor(r.tier), a);
}

const Color kColorPresets[] = {
    {255, 255, 255, 255}, {0, 255, 0, 255},   {127, 255, 0, 255}, {223, 255, 0, 255},
    {255, 255, 0, 255},   {0, 255, 255, 255}, {255, 0, 255, 255}, {255, 60, 60, 255},
};

}  // namespace

// ===========================================================================
// Shared bits

void App::DrawSensSummary(float x, float y, float w, double dpi, double sens) {
    const float tw = (w - 30.0f) / 4.0f;
    StatTile(Rectangle{x, y, tw, 96.0f}, "DPI", Fmt(dpi, 0), theme::kAccent);
    StatTile(Rectangle{x + (tw + 10.0f), y, tw, 96.0f}, "SENS", Fmt(sens, 3), theme::kAccent);
    StatTile(Rectangle{x + 2.0f * (tw + 10.0f), y, tw, 96.0f}, "eDPI", Fmt(val::Edpi(dpi, sens), 0), theme::kText);
    StatTile(Rectangle{x + 3.0f * (tw + 10.0f), y, tw, 96.0f}, "CM / 360", Fmt(val::Cm360(dpi, sens), 1), theme::kText);
}

void App::OpenSettings(Screen returnTo) {
    settingsReturn_ = returnTo;
    rebinding_ = -1;
    SyncSensText();
    xhCodeText_ = EncodeCrosshair(cfg_.crosshair);
    xhMessage_.clear();
    ClearFocus();
    screen_ = Screen::Settings;
}

// ===========================================================================
// First launch

void App::ScreenFirstRun() {
    const float x = ContentX() + 250.0f, w = 1000.0f;
    Title("WELCOME", x, 110.0f, 56.0f);
    TextBlock("Enter your mouse DPI and your current Valorant sensitivity. The trainer turns exactly like Valorant: "
              "every raw mouse count rotates the view by sens x 0.07 degrees, with no Windows acceleration.",
              x, 200.0f, w, 22.0f, theme::kTextDim);

    Text("MOUSE DPI", x, 300.0f, 20.0f, theme::kTextDim);
    TextBox(Rectangle{x, 328.0f, 480.0f, 56.0f}, 1, &dpiText_, 6, true);
    Text("Find it in your mouse software (common: 400, 800, 1600).", x, 392.0f, 18.0f, Alpha(theme::kTextDim, 0.8f));

    Text("VALORANT SENSITIVITY", x + 520.0f, 300.0f, 20.0f, theme::kTextDim);
    TextBox(Rectangle{x + 520.0f, 328.0f, 480.0f, 56.0f}, 2, &sensText_, 8, true);
    Text("Settings > General > Mouse > Sensitivity: Aim.", x + 520.0f, 392.0f, 18.0f, Alpha(theme::kTextDim, 0.8f));

    double dpi = 0.0, sens = 0.0;
    const bool ok = ParseNumber(dpiText_, 50.0, 32000.0, dpi) && ParseNumber(sensText_, 0.001, 20.0, sens);
    if (ok) {
        DrawSensSummary(x, 460.0f, w, dpi, sens);
    } else {
        Text("Enter a DPI (50-32000) and a sensitivity (0.001-20).", x, 490.0f, 22.0f, theme::kWarn);
    }

    if (Button(Rectangle{x, 620.0f, 300.0f, 64.0f}, "CONTINUE", true, ok) ||
        (ok && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))) {
        cfg_.dpi = dpi;
        cfg_.sens = sens;
        cfg_.firstRunDone = true;
        SaveConfig();
        ClearFocus();
        screen_ = Screen::MainMenu;
    }
    if (!rawInputOk_) Text("Warning: Raw Input registration failed.", x, 720.0f, 20.0f, theme::kAccent);
}

// ===========================================================================
// Main menu

void App::ScreenMainMenu() {
    const float x0 = ContentX();
    Logo(x0, 52.0f, 104.0f);
    TextBold("VALTRAINER", x0 + 126.0f, 58.0f, 64.0f, theme::kText);
    Text("Aim trainer calibrated for Valorant  |  raw input  |  0.07 deg/count  |  103 HFOV", x0 + 130.0f, 132.0f, 20.0f,
         theme::kTextDim);
    Text(TextFormat("v%s", kAppVersion), x0 + kContentWidth, VH() - 40.0f, 18.0f, Alpha(theme::kTextDim, 0.7f), Align::Right);

    // Mode cards: 2 columns x 3 rows.
    const float cw = 470.0f, ch = 170.0f, gap = 18.0f;
    for (int i = 0; i < kPlayableModeCount; ++i) {
        const ModeId m = static_cast<ModeId>(i);
        const float cx = x0 + static_cast<float>(i % 2) * (cw + gap);
        const float cy = 200.0f + static_cast<float>(i / 2) * (ch + gap);
        long long best = 0;
        std::string footer = stats_.BestScore(m, best) ? "BEST " + std::to_string(best) + "   " : "";
        AimRank mr;
        if (ModeRank(stats_, m, mr)) footer += RankLabel(mr) + "   ";
        footer += ">  PLAY";
        if (Card(Rectangle{cx, cy, cw, ch}, m, ModeName(m), ModeDescription(m), footer, i + 1)) {
            StartRun(m, false);
            return;
        }
    }
    Text(TextFormat("Runs last %d s (change in Settings > Gameplay)  |  Esc pause  |  %s restart  |  %s FPS counter",
                    cfg_.runSeconds, input::BindName(cfg_.keys.restart).c_str(), input::BindName(cfg_.keys.toggleFps).c_str()),
         x0, 200.0f + 3.0f * (ch + gap) + 6.0f, 18.0f, theme::kTextDim);

    // Right column.
    const float rx = x0 + 2.0f * (cw + gap) + 40.0f;
    const float rw = kContentWidth - (rx - x0);
    Angled(Rectangle{rx, 200.0f, rw, 250.0f}, theme::kPanel, 16.0f);
    Text("YOUR SENSITIVITY", rx + 24.0f, 220.0f, 20.0f, theme::kAccent);
    Text(TextFormat("%.3f  @  %.0f DPI", cfg_.sens, cfg_.dpi), rx + 24.0f, 254.0f, 40.0f, theme::kText);
    Text(TextFormat("eDPI %.0f", val::Edpi(cfg_.dpi, cfg_.sens)), rx + 24.0f, 312.0f, 24.0f, theme::kTextDim);
    Text(TextFormat("%.1f cm / 360", val::Cm360(cfg_.dpi, cfg_.sens)), rx + 24.0f, 346.0f, 24.0f, theme::kTextDim);
    if (Button(Rectangle{rx + 24.0f, 388.0f, rw - 48.0f, 46.0f}, "CHANGE")) {
        settingsTab_ = 0;
        OpenSettings(Screen::MainMenu);
        return;
    }

    float by = 480.0f;
    if (Button(Rectangle{rx, by, rw, 64.0f}, "SENS FINDER", true)) {
        screen_ = Screen::FinderIntro;
        return;
    }
    by += 80.0f;
    if (Button(Rectangle{rx, by, rw, 64.0f}, "STATS & PROGRESS")) {
        screen_ = Screen::Stats;
        return;
    }
    by += 80.0f;
    if (Button(Rectangle{rx, by, rw, 64.0f}, "SETTINGS")) {
        OpenSettings(Screen::MainMenu);
        return;
    }
    by += 80.0f;
    // Minimize (works in every display mode) and quit, side by side.
    const float half = (rw - 12.0f) * 0.5f;
    if (Button(Rectangle{rx, by, half, 64.0f}, "MINIMIZE")) {
        MinimizeWindow();
        return;
    }
    if (Button(Rectangle{rx + half + 12.0f, by, half, 64.0f}, "QUIT")) {
        quit_ = true;
        return;
    }

    // Estimated aim rank (click for the full rank screen).
    const Rectangle rp = {rx, by + 88.0f, rw, 170.0f};
    const float ra = HoverAnim(rp);
    Angled(rp, Lerp2(theme::kPanel, theme::kPanel2, ra), 16.0f);
    Text("AIM RANK (ESTIMATE)", rp.x + 24.0f, rp.y + 14.0f, 18.0f, theme::kAccent);
    Text("DETAILS >", rp.x + rp.width - 24.0f, rp.y + 14.0f, 16.0f, Lerp2(theme::kTextDim, theme::kText, ra), Align::Right);
    AimRank overall;
    int modesUsed = 0;
    if (OverallRank(stats_, overall, &modesUsed)) {
        DrawRankBadge(rp.x + 72.0f, rp.y + 92.0f, 90.0f, overall);
        RankText(overall, rp.x + 138.0f, rp.y + 44.0f, 34.0f);
        TextBlock(RankComment(overall, static_cast<unsigned int>(overall.points * 100.0)), rp.x + 138.0f, rp.y + 88.0f,
                  rp.width - 160.0f, 16.0f, theme::kText);
    } else {
        TextBlock(TextFormat("Play %d different modes (20 s+ runs) to get your overall aim rank. Modes ranked: %d / %d.",
                             kMinModesForOverall, modesUsed, kMinModesForOverall),
                  rp.x + 24.0f, rp.y + 48.0f, rp.width - 48.0f, 18.0f, theme::kTextDim);
    }
    if (Hover(rp) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        screen_ = Screen::Rank;
        return;
    }

    if (selfTestRan_) {
        Text(selfTestOk_ ? "Debug build: math self-test PASSED" : "Debug build: math self-test FAILED (see log)", x0,
             VH() - 40.0f, 18.0f, selfTestOk_ ? theme::kGood : theme::kAccent);
    }
    if (!rawInputOk_) Text("Raw Input registration failed!", x0 + 500.0f, VH() - 40.0f, 18.0f, theme::kAccent);
}

// ===========================================================================
// Results

void App::ScreenResults() {
    const float x0 = ContentX();
    const RunStats& s = lastStats_;
    Title(std::string(ModeName(s.mode)) + "  -  RESULTS", x0, 60.0f, 52.0f);
    if (lastWasPb_) {
        Angled(Rectangle{x0 + 6.0f, 136.0f, 330.0f, 40.0f}, theme::kAccent, 10.0f);
        Text(hadPreviousBest_ ? "NEW PERSONAL BEST" : "FIRST RUN - PB SET", x0 + 171.0f, 144.0f, 22.0f, theme::kText,
             Align::Center);
    } else {
        Text("Personal best: " + std::to_string(previousBest_), x0 + 6.0f, 144.0f, 22.0f, theme::kTextDim);
    }

    const float tw = (kContentWidth - 30.0f) / 4.0f, th = 100.0f;
    auto tile = [&](int col, int row, const std::string& title, const std::string& value, Color c) {
        StatTile(Rectangle{x0 + static_cast<float>(col) * (tw + 10.0f), 200.0f + static_cast<float>(row) * (th + 12.0f), tw, th},
                 title, value, c);
    };
    // Score counts up over 0.7 s (ease-out).
    const double k = std::min(1.0, (platform::Now() - resultsShownAt_) / 0.7);
    const double eased = 1.0 - (1.0 - k) * (1.0 - k) * (1.0 - k);
    const long long shown = static_cast<long long>(std::llround(static_cast<double>(s.score) * eased));
    std::string scoreTitle = "SCORE";
    if (hadPreviousBest_ && previousBest_ > 0) {
        const double vsPb = (static_cast<double>(s.score) / static_cast<double>(previousBest_) - 1.0) * 100.0;
        scoreTitle = TextFormat("SCORE  (%+.0f%% vs PB)", vsPb);
    }
    tile(0, 0, scoreTitle, std::to_string(shown), theme::kAccent);
    tile(1, 0, s.mode == ModeId::Tracking ? "ACCURACY (ON TARGET WHILE FIRING)" : "ACCURACY", Fmt(s.Accuracy() * 100.0, 1) + "%",
         theme::kAccent);
    if (s.mode == ModeId::Tracking) {
        tile(2, 0, "TIME ON TARGET", Fmt(s.trackOnTime, 1) + " s", theme::kText);
        tile(3, 0, "TRACKING %", Fmt(std::max(0.0, s.TrackingPct()) * 100.0, 1) + "%", theme::kText);
    } else {
        tile(2, 0, "HITS", std::to_string(s.hits), theme::kGood);
        tile(3, 0, s.expired > 0 ? TextFormat("MISSES  (+%d EXPIRED)", s.expired) : "MISSES", std::to_string(s.Misses()),
             theme::kWarn);
    }
    tile(0, 1, "AVG REACTION", MsOrDash(s.AvgReactionMs()), theme::kText);
    tile(1, 1, "AVG TIME TO KILL", MsOrDash(s.AvgTtkMs()), theme::kText);
    const double share = s.OvershootShare();
    tile(2, 1, TextFormat("OVER / UNDER  (%d / %d)", s.overshoots, s.undershoots),
         share >= 0.0 ? (share >= 0.5 ? Fmt(share * 100.0, 0) + "% OVER" : Fmt((1.0 - share) * 100.0, 0) + "% UNDER") : "-",
         theme::kText);
    tile(3, 1, "AVG CLICK ERROR", s.MeanErrDeg() >= 0.0 ? Fmt(s.MeanErrDeg(), 2) + " deg" : "-", theme::kText);

    // Coaching tips.
    // Rank strip: this run's rank, progress, mode rank and a comment.
    {
        const Rectangle rr = {x0, 424.0f, kContentWidth, 104.0f};
        Angled(rr, theme::kPanel, 14.0f);
        if (lastRunRanked_) {
            Fill(Rectangle{rr.x, rr.y, 5.0f, rr.height - 14.0f}, TierColor(lastRunRank_.tier));
            DrawRankBadge(rr.x + 56.0f, rr.y + 46.0f, 66.0f, lastRunRank_);
            Text("THIS RUN", rr.x + 110.0f, rr.y + 12.0f, 16.0f, theme::kTextDim);
            RankText(lastRunRank_, rr.x + 110.0f, rr.y + 32.0f, 30.0f);
            if (lastRunRank_.tier < kTierCount - 1) {
                const Rectangle bar = {rr.x + 330.0f, rr.y + 44.0f, 330.0f, 8.0f};
                Fill(bar, theme::kBg);
                Fill(Rectangle{bar.x, bar.y, bar.width * static_cast<float>(lastRunRank_.progress), bar.height},
                     TierColor(lastRunRank_.tier));
                const AimRank next = RankFromPoints(std::floor(lastRunRank_.points) + 1.0);
                Text(TextFormat("%.0f%% to %s", lastRunRank_.progress * 100.0, RankLabel(next).c_str()), bar.x, rr.y + 18.0f,
                     16.0f, theme::kTextDim);
            }
            if (lastModeRanked_) {
                Text(TextFormat("%s RANK (LAST 5 RUNS)", ModeName(s.mode)), rr.x + 720.0f, rr.y + 12.0f, 16.0f, theme::kTextDim);
                RankText(lastModeRank_, rr.x + 720.0f, rr.y + 32.0f, 30.0f);
            }
            Text("Aim-only estimate", rr.x + rr.width - 24.0f, rr.y + 14.0f, 16.0f, Alpha(theme::kTextDim, 0.7f), Align::Right);
            Text(RankComment(lastRunRank_, static_cast<unsigned int>(lastRunRank_.points * 1000.0)), rr.x + 110.0f,
                 rr.y + 72.0f, 20.0f, TierColor(lastRunRank_.tier));
        } else {
            Text(s.duration < kMinRankedSeconds ? "Runs shorter than 20 s are not ranked."
                                                : "Not enough hits in this run to estimate a rank.",
                 rr.x + 24.0f, rr.y + 40.0f, 20.0f, theme::kTextDim);
        }
    }

    const Rectangle tipsR = {x0, 540.0f, kContentWidth, 200.0f};
    Angled(tipsR, theme::kPanel, 16.0f);
    Text("COACH", tipsR.x + 24.0f, tipsR.y + 18.0f, 22.0f, theme::kAccent);
    float ty = tipsR.y + 56.0f;
    for (const std::string& tip : lastTips_) {
        Fill(Rectangle{tipsR.x + 24.0f, ty + 9.0f, 8.0f, 8.0f}, theme::kAccent);
        ty += TextBlock(tip, tipsR.x + 44.0f, ty, tipsR.width - 80.0f, 22.0f, theme::kText) + 10.0f;
    }

    // Automatic coach sens change (Settings > Sensitivity > Auto-adjust).
    if (autoSensApplied_) {
        const Rectangle ar = {x0, 752.0f, kContentWidth, 56.0f};
        Angled(ar, theme::kPanel2, 12.0f);
        Fill(Rectangle{ar.x, ar.y, 5.0f, ar.height - 12.0f}, theme::kWarn);
        Text(TextFormat("Coach changed your sens %.3f -> %.3f (%+d%%). Set it in Valorant too.", autoSensBefore_, cfg_.sens,
                        autoSensPct_),
             ar.x + 24.0f, ar.y + 16.0f, 22.0f, theme::kText);
        if (Button(Rectangle{ar.x + ar.width - 190.0f, ar.y + 6.0f, 180.0f, 44.0f}, "UNDO")) {
            SetSens(autoSensBefore_);
            autoSensApplied_ = false;
        }
    }

    const float by = 830.0f;
    if (Button(Rectangle{x0, by, 300.0f, 64.0f}, "PLAY AGAIN", true) || input::BindPressed(cfg_.keys.restart) ||
        IsKeyPressed(KEY_ENTER)) {
        StartRun(s.mode, false);
        return;
    }
    if (Button(Rectangle{x0 + 320.0f, by, 260.0f, 64.0f}, "STATS")) {
        statsMode_ = static_cast<int>(s.mode);
        screen_ = Screen::Stats;
        return;
    }
    if (Button(Rectangle{x0 + 600.0f, by, 260.0f, 64.0f}, "MAIN MENU") || IsKeyPressed(KEY_ESCAPE)) {
        screen_ = Screen::MainMenu;
        return;
    }
    const double playedSens = autoSensApplied_ ? autoSensBefore_ : cfg_.sens;
    Text(TextFormat("Played at sens %.3f / %.0f DPI (%.1f cm/360). Saved to stats.csv.", playedSens, cfg_.dpi,
                    val::Cm360(cfg_.dpi, playedSens)),
         x0, by + 90.0f, 18.0f, theme::kTextDim);
}

// ===========================================================================
// Settings

void App::ScreenSettings() {
    const float x0 = ContentX();
    Title("SETTINGS", x0, 50.0f, 52.0f);

    static const char* const tabs[] = {"SENSITIVITY", "VIDEO", "GAMEPLAY", "CROSSHAIR", "AUDIO", "KEYBINDS"};
    const float tabW = kContentWidth / 6.0f;
    for (int i = 0; i < 6; ++i) {
        if (Tab(Rectangle{x0 + static_cast<float>(i) * tabW, 130.0f, tabW, 52.0f}, tabs[i], settingsTab_ == i)) {
            settingsTab_ = i;
            rebinding_ = -1;
            ClearFocus();
        }
    }
    Fill(Rectangle{x0, 182.0f, kContentWidth, 1.0f}, theme::kLine);

    const bool wasRebinding = rebinding_ >= 0;
    const float y = 220.0f;
    switch (settingsTab_) {
        case 0: SettingsSensitivity(x0, y, kContentWidth); break;
        case 1: SettingsVideo(x0, y, kContentWidth); break;
        case 2: SettingsGameplay(x0, y, kContentWidth); break;
        case 3: SettingsCrosshair(x0, y, kContentWidth); break;
        case 4: SettingsAudio(x0, y, kContentWidth); break;
        default: SettingsKeybinds(x0, y, kContentWidth); break;
    }
    if (screen_ != Screen::Settings) return;

    const bool escBack = IsKeyPressed(KEY_ESCAPE) && !wasRebinding && !AnyTextBoxFocused();
    if (Button(Rectangle{x0, VH() - 100.0f, 260.0f, 60.0f}, "BACK", true) || escBack) {
        rebinding_ = -1;
        ClearFocus();
        SyncSensText();
        SaveConfig();
        screen_ = settingsReturn_;
    }
    Text("Settings are saved to config.ini next to the .exe.", x0 + 290.0f, VH() - 80.0f, 18.0f, theme::kTextDim);
}

void App::SettingsSensitivity(float x, float y, float w) {
    Text("MOUSE DPI", x, y, 20.0f, theme::kTextDim);
    double v = 0.0;
    if (TextBox(Rectangle{x, y + 28.0f, 360.0f, 54.0f}, 10, &dpiText_, 6, true) && ParseNumber(dpiText_, 50.0, 32000.0, v)) {
        cfg_.dpi = v;
    }
    Text("VALORANT SENSITIVITY", x + 400.0f, y, 20.0f, theme::kTextDim);
    if (TextBox(Rectangle{x + 400.0f, y + 28.0f, 360.0f, 54.0f}, 11, &sensText_, 8, true) &&
        ParseNumber(sensText_, 0.001, 20.0, v)) {
        cfg_.sens = v;
    }
    DrawSensSummary(x, y + 120.0f, w, cfg_.dpi, cfg_.sens);

    TextBlock("How it works: Valorant turns 0.07 degrees per mouse count at sensitivity 1.0, so each count turns "
              "sens x 0.07 degrees. cm/360 = 360 / (DPI x sens x 0.07) x 2.54. The trainer reads Raw Input, so Windows "
              "pointer speed and 'Enhance pointer precision' have no effect - exactly like in Valorant. There is no "
              "smoothing or acceleration of any kind.",
              x, y + 260.0f, w * 0.75f, 20.0f, theme::kTextDim);
    TextBlock(TextFormat("One count at your sens = %.4f degrees. A full 360 needs %.0f counts.", val::DegreesPerCount(cfg_.sens),
                         360.0 / val::DegreesPerCount(cfg_.sens)),
              x, y + 390.0f, w * 0.75f, 20.0f, theme::kText);

    Toggle(Rectangle{x, y + 450.0f, std::min(760.0f, w), 50.0f}, "Auto-adjust sens from coach", &cfg_.autoSens);
    TextBlock("When on, the coach's over/undershoot suggestion (for example \"you overshoot, lower sens ~5%\") is "
              "applied to your sens automatically after each run, 2-15% at a time. It only changes when a run has enough "
              "flick data (6+ directional misses) and a clear tendency. The results screen shows the change and has "
              "an UNDO button. Remember to copy the new value into Valorant.",
              x, y + 510.0f, w * 0.75f, 20.0f, theme::kTextDim);
}

void App::SettingsVideo(float x, float y, float w) {
    static const char* const modes[] = {"Fullscreen", "Borderless", "Windowed"};
    int dm = static_cast<int>(cfg_.displayMode);
    const float cw = std::min(760.0f, w);
    if (Stepper(Rectangle{x, y, cw, 50.0f}, "Display mode", &dm, modes, 3)) {
        cfg_.displayMode = static_cast<DisplayMode>(dm);
        ApplyDisplayMode();
    }
    Stepper(Rectangle{x, y + 70.0f, cw, 50.0f}, "FPS cap", &cfg_.fpsCapIndex, kFpsCapNames, kFpsCapCount);
    if (cfg_.fpsCapIndex == kFpsCapCustomIndex) {
        // Any cap from 30 to 2000 FPS.
        double fps = 0.0;
        if (TextBox(Rectangle{x + cw + 20.0f, y + 70.0f, 140.0f, 50.0f}, 12, &customFpsText_, 4, true) &&
            ParseNumber(customFpsText_, kCustomFpsMin, kCustomFpsMax, fps)) {
            cfg_.customFpsCap = static_cast<int>(std::lround(fps));
        }
        Text(TextFormat("FPS  (%d-%d, now %d)", kCustomFpsMin, kCustomFpsMax, cfg_.FpsCap()), x + cw + 175.0f, y + 84.0f,
             20.0f, theme::kTextDim);
    }
    Toggle(Rectangle{x, y + 140.0f, cw, 50.0f}, "Show FPS counter + frame time", &cfg_.showFps);
    Toggle(Rectangle{x, y + 200.0f, cw, 50.0f}, "Show FOV / sens info in game", &cfg_.showFovInfo);

    const double aspect = static_cast<double>(GetScreenWidth()) / std::max(1, GetScreenHeight());
    StatTile(Rectangle{x, y + 280.0f, 240.0f, 96.0f}, "HORIZONTAL FOV", Fmt(val::GameHorizontalFov(aspect), 2) + " deg",
             theme::kAccent);
    StatTile(Rectangle{x + 250.0f, y + 280.0f, 240.0f, 96.0f}, "VERTICAL FOV", Fmt(val::GameVerticalFov(), 2) + " deg",
             theme::kAccent);
    StatTile(Rectangle{x + 500.0f, y + 280.0f, 260.0f, 96.0f}, "RESOLUTION",
             TextFormat("%dx%d", GetScreenWidth(), GetScreenHeight()), theme::kText);
    TextBlock("FOV is fixed like Valorant: 103 degrees horizontal on a 16:9 screen, with Hor+ scaling (the vertical FOV "
              "stays at 70.53 degrees and the horizontal FOV follows your aspect ratio). V-Sync is always off. The menus "
              "are limited to 240 FPS to keep your GPU cool; gameplay uses the FPS cap above. If you still see a cap, "
              "disable V-Sync / frame limiters for this app in your GPU control panel.",
              x, y + 410.0f, cw, 20.0f, theme::kTextDim);
}

void App::SettingsGameplay(float x, float y, float w) {
    const float cw = std::min(700.0f, w * 0.5f);
    SliderI(Rectangle{x, y, cw, 54.0f}, "Run length (seconds)", &cfg_.runSeconds, 10, 300);
    cfg_.runSeconds = std::max(10, (cfg_.runSeconds + 2) / 5 * 5);
    SliderF(Rectangle{x, y + 80.0f, cw, 54.0f}, "Map brightness", &cfg_.mapBrightness, 0.2f, 1.6f, "%.2f");

    Toggle(Rectangle{x, y + 470.0f, cw, 50.0f}, "Roast mode (funny rank comments)", &cfg_.roastMode);
    Text("TARGET COLOUR", x, y + 170.0f, 20.0f, theme::kTextDim);
    const Color targetPresets[] = {{80, 220, 255, 255}, {255, 75, 87, 255},  {255, 220, 40, 255},
                                   {90, 255, 120, 255}, {255, 110, 230, 255}, {255, 255, 255, 255}};
    for (int i = 0; i < 6; ++i) {
        const Color c = targetPresets[i];
        const bool sel = c.r == cfg_.targetColor.r && c.g == cfg_.targetColor.g && c.b == cfg_.targetColor.b;
        if (Swatch(Rectangle{x + static_cast<float>(i) * 58.0f, y + 200.0f, 46.0f, 46.0f}, c, sel)) cfg_.targetColor = c;
    }
    SliderU8(Rectangle{x, y + 270.0f, cw, 50.0f}, "Red", &cfg_.targetColor.r);
    SliderU8(Rectangle{x, y + 330.0f, cw, 50.0f}, "Green", &cfg_.targetColor.g);
    SliderU8(Rectangle{x, y + 390.0f, cw, 50.0f}, "Blue", &cfg_.targetColor.b);

    // Preview: target colour on the map colour at the chosen brightness.
    const Rectangle prev = {x + cw + 80.0f, y, 420.0f, 440.0f};
    Fill(prev, Shade(Color{44, 47, 55, 255}, cfg_.mapBrightness));
    Fill(Rectangle{prev.x, prev.y, prev.width, 200.0f}, Shade(Color{58, 62, 72, 255}, cfg_.mapBrightness));
    const Rectangle s = ToScreen(prev);
    DrawCircleV(Vector2{s.x + s.width * 0.5f, s.y + s.height * 0.45f}, 60.0f * Scale(), cfg_.targetColor);
    Border(prev, 1.0f, theme::kLine);
    Text("PREVIEW", prev.x, prev.y + prev.height + 10.0f, 18.0f, theme::kTextDim);
}

void App::SettingsCrosshair(float x, float y, float /*w*/) {
    Crosshair& c = cfg_.crosshair;
    bool changed = false;
    const float colW = 440.0f, rowH = 52.0f;

    // Column 1: colour, outline, centre dot.
    float cy = y;
    Text("COLOUR", x, cy, 20.0f, theme::kTextDim);
    for (int i = 0; i < 8; ++i) {
        const Color p = kColorPresets[i];
        const bool sel = p.r == c.r && p.g == c.g && p.b == c.b;
        if (Swatch(Rectangle{x + static_cast<float>(i) * 54.0f, cy + 28.0f, 44.0f, 36.0f}, p, sel)) {
            c.r = p.r;
            c.g = p.g;
            c.b = p.b;
            changed = true;
        }
    }
    cy += 78.0f;
    changed |= SliderU8(Rectangle{x, cy, colW, 46.0f}, "Red", &c.r);
    changed |= SliderU8(Rectangle{x, cy + rowH, colW, 46.0f}, "Green", &c.g);
    changed |= SliderU8(Rectangle{x, cy + 2.0f * rowH, colW, 46.0f}, "Blue", &c.b);
    cy += 3.0f * rowH + 10.0f;
    changed |= Toggle(Rectangle{x, cy, colW, 40.0f}, "Outlines", &c.outline);
    changed |= SliderI(Rectangle{x, cy + rowH, colW, 46.0f}, "Outline thickness", &c.outlineThickness, 1, 6);
    changed |= SliderF(Rectangle{x, cy + 2.0f * rowH, colW, 46.0f}, "Outline opacity", &c.outlineOpacity, 0.0f, 1.0f, "%.2f");
    cy += 3.0f * rowH + 10.0f;
    changed |= Toggle(Rectangle{x, cy, colW, 40.0f}, "Center dot", &c.centerDot);
    changed |= SliderI(Rectangle{x, cy + rowH, colW, 46.0f}, "Center dot thickness", &c.dotThickness, 1, 6);
    changed |= SliderF(Rectangle{x, cy + 2.0f * rowH, colW, 46.0f}, "Center dot opacity", &c.dotOpacity, 0.0f, 1.0f, "%.2f");

    // Column 2: inner and outer lines.
    const float x2 = x + colW + 50.0f;
    cy = y;
    changed |= Toggle(Rectangle{x2, cy, colW, 40.0f}, "Inner lines", &c.innerShow);
    changed |= SliderF(Rectangle{x2, cy + rowH, colW, 46.0f}, "Inner line opacity", &c.innerOpacity, 0.0f, 1.0f, "%.2f");
    changed |= SliderI(Rectangle{x2, cy + 2.0f * rowH, colW, 46.0f}, "Inner line length", &c.innerLength, 0, 20);
    changed |= SliderI(Rectangle{x2, cy + 3.0f * rowH, colW, 46.0f}, "Inner line thickness", &c.innerThickness, 1, 10);
    changed |= SliderI(Rectangle{x2, cy + 4.0f * rowH, colW, 46.0f}, "Inner line offset", &c.innerOffset, 0, 20);
    cy += 5.0f * rowH + 20.0f;
    changed |= Toggle(Rectangle{x2, cy, colW, 40.0f}, "Outer lines", &c.outerShow);
    changed |= SliderF(Rectangle{x2, cy + rowH, colW, 46.0f}, "Outer line opacity", &c.outerOpacity, 0.0f, 1.0f, "%.2f");
    changed |= SliderI(Rectangle{x2, cy + 2.0f * rowH, colW, 46.0f}, "Outer line length", &c.outerLength, 0, 20);
    changed |= SliderI(Rectangle{x2, cy + 3.0f * rowH, colW, 46.0f}, "Outer line thickness", &c.outerThickness, 1, 10);
    changed |= SliderI(Rectangle{x2, cy + 4.0f * rowH, colW, 46.0f}, "Outer line offset", &c.outerOffset, 0, 40);

    // Column 3: live preview + share code.
    const float x3 = x2 + colW + 50.0f;
    const float pw = ContentX() + kContentWidth - x3;
    const Rectangle darkR = {x3, y, pw * 0.5f - 5.0f, 200.0f};
    const Rectangle lightR = {x3 + pw * 0.5f + 5.0f, y, pw * 0.5f - 5.0f, 200.0f};
    const Rectangle zoomR = {x3, y + 210.0f, pw, 230.0f};
    Fill(darkR, Color{40, 44, 52, 255});
    Fill(lightR, Color{170, 176, 186, 255});
    Fill(zoomR, Color{60, 64, 74, 255});
    auto center = [](Rectangle r) {
        const Rectangle s = ToScreen(r);
        return Vector2{std::floor(s.x + s.width * 0.5f), std::floor(s.y + s.height * 0.5f)};
    };
    const Vector2 c1 = center(darkR), c2 = center(lightR), c3 = center(zoomR);
    DrawCrosshair(c, static_cast<int>(c1.x), static_cast<int>(c1.y), 1);
    DrawCrosshair(c, static_cast<int>(c2.x), static_cast<int>(c2.y), 1);
    DrawCrosshair(c, static_cast<int>(c3.x), static_cast<int>(c3.y), 4);
    Text("ACTUAL SIZE", darkR.x + 8.0f, darkR.y + 6.0f, 16.0f, theme::kTextDim);
    Text("4x ZOOM", zoomR.x + 8.0f, zoomR.y + 6.0f, 16.0f, theme::kTextDim);

    if (changed) {
        ClampCrosshair(c);
        xhCodeText_ = EncodeCrosshair(c);
        xhMessage_.clear();
    }

    float sy = y + 460.0f;
    Text("CROSSHAIR CODE (Valorant share code or XH1 code, then IMPORT)", x3, sy, 18.0f, theme::kTextDim);
    TextBox(Rectangle{x3, sy + 26.0f, pw, 48.0f}, 20, &xhCodeText_, 1000, false);
    sy += 90.0f;
    const float bw = (pw - 20.0f) / 3.0f;
    if (Button(Rectangle{x3, sy, bw, 48.0f}, "IMPORT", true)) {
        Crosshair parsed;
        std::string err;
        const bool fromValorant = xhCodeText_.find(';') != std::string::npos &&
                                  xhCodeText_.find_first_not_of(" \t") == xhCodeText_.find("0;");
        if (DecodeAnyCrosshair(xhCodeText_, parsed, &err)) {
            c = parsed;
            xhCodeText_ = EncodeCrosshair(c);
            xhMessage_ = fromValorant ? "Imported your Valorant crosshair."
                                      : "Imported.";
        } else {
            xhMessage_ = "Invalid code: " + err;
        }
    }
    if (Button(Rectangle{x3 + bw + 10.0f, sy, bw, 48.0f}, "COPY")) {
        SetClipboardText(EncodeCrosshair(c).c_str());
        xhMessage_ = "Copied to clipboard.";
    }
    if (Button(Rectangle{x3 + 2.0f * (bw + 10.0f), sy, bw, 48.0f}, "PASTE")) {
        const char* clip = GetClipboardText();
        xhCodeText_ = clip ? clip : "";
        xhMessage_ = "Pasted - press IMPORT to apply.";
    }
    if (Button(Rectangle{x3, sy + 60.0f, pw, 44.0f}, "RESET TO DEFAULT")) {
        c = Crosshair{};
        xhCodeText_ = EncodeCrosshair(c);
        xhMessage_ = "Reset.";
    }
    if (!xhMessage_.empty()) TextBlock(xhMessage_, x3, sy + 118.0f, pw, 18.0f, theme::kWarn);
    TextBlock("Format: XH1; c=RRGGBB colour; o/ot/oa = outline on, thickness, alpha; d/dt/da = center dot; "
              "i/ia/il/it/io = inner lines on, alpha, length, thickness, offset; x/xa/xl/xt/xo = outer lines.",
              x3, sy + 175.0f, pw, 16.0f, Alpha(theme::kTextDim, 0.8f));
}

void App::SettingsAudio(float x, float y, float w) {
    const float cw = std::min(700.0f, w);
    if (SliderF(Rectangle{x, y, cw, 54.0f}, "Master volume", &cfg_.masterVolume, 0.0f, 1.0f, "%.2f")) ApplyAudio();
    if (SliderF(Rectangle{x, y + 80.0f, cw, 54.0f}, "Hit sound volume", &cfg_.hitVolume, 0.0f, 1.0f, "%.2f")) ApplyAudio();
    const float bw = (cw - 30.0f) / 4.0f;
    if (Button(Rectangle{x, y + 170.0f, bw, 50.0f}, "TEST HIT")) audio_.Play(Sfx::Kill);
    if (Button(Rectangle{x + bw + 10.0f, y + 170.0f, bw, 50.0f}, "HEADSHOT")) audio_.Play(Sfx::Headshot);
    if (Button(Rectangle{x + 2.0f * (bw + 10.0f), y + 170.0f, bw, 50.0f}, "MISS")) audio_.Play(Sfx::Miss);
    if (Button(Rectangle{x + 3.0f * (bw + 10.0f), y + 170.0f, bw, 50.0f}, "TICK")) audio_.Play(Sfx::Tick);
    TextBlock("All sounds are generated in code when the app starts - there are no audio files.", x, y + 250.0f, cw, 20.0f,
              theme::kTextDim);
}

void App::SettingsKeybinds(float x, float y, float w) {
    struct Row {
        const char* name;
        int* code;
        bool allowLeftMouse;
    };
    Row rows[] = {{"Shoot", &cfg_.keys.shoot, true},
                  {"Restart run", &cfg_.keys.restart, false},
                  {"Pause (Esc always pauses too)", &cfg_.keys.pause, false},
                  {"Toggle FPS counter", &cfg_.keys.toggleFps, false}};
    const float cw = std::min(900.0f, w);

    // Capture the next key/button (skipping the click that started capture).
    if (rebinding_ >= 0 && frameCounter_ > rebindStartFrame_) {
        const int code = input::CaptureBind();
        if (code == -1) {
            rebinding_ = -1;
        } else if (code > 0) {
            Row& r = rows[rebinding_];
            if (!r.allowLeftMouse && code == input::MouseBind(0)) {
                xhMessage_ = "Mouse 1 is reserved for shooting and menus.";
            } else {
                *r.code = code;
                xhMessage_.clear();
            }
            rebinding_ = -1;
        }
    }

    for (int i = 0; i < 4; ++i) {
        const float ry = y + static_cast<float>(i) * 72.0f;
        Fill(Rectangle{x, ry, cw, 60.0f}, theme::kPanel);
        Text(rows[i].name, x + 20.0f, ry + 18.0f, 22.0f, theme::kText);
        const std::string label = rebinding_ == i ? "PRESS A KEY / BUTTON  (Esc cancels)" : input::BindName(*rows[i].code);
        if (Button(Rectangle{x + cw - 420.0f, ry + 6.0f, 410.0f, 48.0f}, label, rebinding_ == i) && rebinding_ < 0) {
            rebinding_ = i;
            rebindStartFrame_ = frameCounter_;
        }
    }
    if (!xhMessage_.empty() && settingsTab_ == 5) Text(xhMessage_, x, y + 300.0f, 20.0f, theme::kWarn);
    TextBlock("Shooting on a mouse button is read directly from Raw Input together with the movement, so each click is "
              "evaluated at the exact crosshair position it happened at. A keyboard shoot key is evaluated per frame.",
              x, y + 340.0f, cw, 20.0f, theme::kTextDim);
}

// ===========================================================================
// Stats & progress

void App::ScreenStats() {
    const float x0 = ContentX();
    Title("STATS & PROGRESS", x0, 50.0f, 52.0f);
    const float tabW = kContentWidth / static_cast<float>(kPlayableModeCount);
    for (int i = 0; i < kPlayableModeCount; ++i) {
        if (Tab(Rectangle{x0 + static_cast<float>(i) * tabW, 130.0f, tabW, 52.0f}, ModeName(static_cast<ModeId>(i)),
                statsMode_ == i)) {
            statsMode_ = i;
        }
    }
    Fill(Rectangle{x0, 182.0f, kContentWidth, 1.0f}, theme::kLine);

    const ModeId m = static_cast<ModeId>(std::clamp(statsMode_, 0, kPlayableModeCount - 1));
    const std::vector<const RunRecord*> runs = stats_.ForMode(m);

    // Personal bests.
    long long bestScore = 0;
    double bestAcc = 0.0, bestReaction = 0.0, bestTtk = 0.0, bestTrack = 0.0;
    for (const RunRecord* r : runs) {
        bestScore = std::max(bestScore, r->score);
        bestAcc = std::max(bestAcc, r->accuracy);
        if (r->avgReactionMs > 0.0 && (bestReaction <= 0.0 || r->avgReactionMs < bestReaction)) bestReaction = r->avgReactionMs;
        if (r->avgTtkMs > 0.0 && (bestTtk <= 0.0 || r->avgTtkMs < bestTtk)) bestTtk = r->avgTtkMs;
        bestTrack = std::max(bestTrack, r->trackingPct);
    }
    const float tw = (kContentWidth - 40.0f) / 5.0f;
    const float ty = 206.0f;
    {
        AimRank mr;
        int ranked = 0;
        const bool has = ModeRank(stats_, m, mr, &ranked);
        StatTile(Rectangle{x0, ty, tw, 96.0f}, TextFormat("RANK  (%d runs)", static_cast<int>(runs.size())),
                 has ? RankLabel(mr) : "-", has ? TierColor(mr.tier) : theme::kText);
    }
    StatTile(Rectangle{x0 + (tw + 10.0f), ty, tw, 96.0f}, "BEST SCORE", runs.empty() ? "-" : std::to_string(bestScore),
             theme::kAccent);
    StatTile(Rectangle{x0 + 2.0f * (tw + 10.0f), ty, tw, 96.0f}, "BEST ACCURACY", runs.empty() ? "-" : Fmt(bestAcc, 1) + "%",
             theme::kAccent);
    if (m == ModeId::Tracking) {
        StatTile(Rectangle{x0 + 3.0f * (tw + 10.0f), ty, tw, 96.0f}, "BEST TRACKING", runs.empty() ? "-" : Fmt(bestTrack, 1) + "%",
                 theme::kAccent);
    } else {
        StatTile(Rectangle{x0 + 3.0f * (tw + 10.0f), ty, tw, 96.0f}, "BEST AVG REACTION", MsOrDash(bestReaction), theme::kAccent);
    }
    StatTile(Rectangle{x0 + 4.0f * (tw + 10.0f), ty, tw, 96.0f}, "BEST AVG TTK", MsOrDash(bestTtk), theme::kAccent);

    // Graphs of the last 60 runs.
    const size_t first = runs.size() > 60 ? runs.size() - 60 : 0;
    std::vector<float> scores, second;
    int bestIdx = -1;
    for (size_t i = first; i < runs.size(); ++i) {
        scores.push_back(static_cast<float>(runs[i]->score));
        second.push_back(static_cast<float>(m == ModeId::Tracking ? std::max(0.0, runs[i]->trackingPct) : runs[i]->accuracy));
        if (runs[i]->score == bestScore && bestIdx < 0) bestIdx = static_cast<int>(i - first);
    }
    const float gw = (kContentWidth - 90.0f) * 0.5f;
    LineChart(Rectangle{x0 + 60.0f, 360.0f, gw - 20.0f, 300.0f}, scores, theme::kAccent, bestIdx,
              "SCORE (last 60 runs, PB highlighted)");
    LineChart(Rectangle{x0 + gw + 110.0f, 360.0f, gw - 20.0f, 300.0f}, second, theme::kGood, -1,
              m == ModeId::Tracking ? "TRACKING % " : "ACCURACY %");

    // Recent runs table.
    float ry = 700.0f;
    Text("RECENT RUNS", x0, ry, 20.0f, theme::kAccent);
    ry += 32.0f;
    const float cols[] = {0.0f, 260.0f, 420.0f, 580.0f, 760.0f, 940.0f, 1120.0f};
    const char* const heads[] = {"DATE", "SCORE", "ACCURACY", "REACTION", "TTK", "OVERSHOOT", "SENS"};
    for (int i = 0; i < 7; ++i) Text(heads[i], x0 + cols[i], ry, 16.0f, theme::kTextDim);
    ry += 26.0f;
    int shown = 0;
    for (auto it = runs.rbegin(); it != runs.rend() && shown < 6; ++it, ++shown) {
        const RunRecord& r = **it;
        const Color c = r.score == bestScore ? theme::kWarn : theme::kText;
        Text(r.timestamp, x0 + cols[0], ry, 18.0f, c);
        Text(std::to_string(r.score), x0 + cols[1], ry, 18.0f, c);
        Text(Fmt(r.accuracy, 1) + "%", x0 + cols[2], ry, 18.0f, c);
        Text(MsOrDash(r.avgReactionMs), x0 + cols[3], ry, 18.0f, c);
        Text(MsOrDash(r.avgTtkMs), x0 + cols[4], ry, 18.0f, c);
        Text(r.overshootPct >= 0.0 ? Fmt(r.overshootPct, 0) + "%" : "-", x0 + cols[5], ry, 18.0f, c);
        Text(TextFormat("%.3f @ %.0f", r.sens, r.dpi), x0 + cols[6], ry, 18.0f, c);
        ry += 28.0f;
    }
    if (runs.empty()) Text("Play this mode to start tracking progress.", x0, ry, 20.0f, theme::kTextDim);

    if (Button(Rectangle{x0, VH() - 100.0f, 260.0f, 60.0f}, "BACK", true) || IsKeyPressed(KEY_ESCAPE)) {
        screen_ = Screen::MainMenu;
        return;
    }
    if (Button(Rectangle{x0 + 280.0f, VH() - 100.0f, 300.0f, 60.0f}, TextFormat("PLAY %s", ModeName(m)))) {
        StartRun(m, false);
        return;
    }
    Text("All runs are stored in stats.csv next to the .exe.", x0 + 610.0f, VH() - 80.0f, 18.0f, theme::kTextDim);
}

// ===========================================================================
// Sens finder

void App::ScreenFinderIntro() {
    const float x0 = ContentX();
    Title("SENS FINDER", x0, 50.0f, 52.0f);
    TextBlock("Perfect Sensitivity Approximation (PSA). Starting from your current sens, you play two blind 20 second "
              "tests per round - one at the top and one at the bottom of the range (first x1.5 and x0.5). Each test mixes "
              "flicks, tracking and micro-adjustments and is scored on accuracy, time-to-kill, click precision (over/"
              "undershoot), tracking and your 1-5 comfort rating. The range then halves toward the better side. After 7 "
              "rounds (about 6 minutes) you get a recommendation. Run it on different days: sessions are saved and averaged.",
              x0, 130.0f, 900.0f, 21.0f, theme::kTextDim);

    Text("STARTING POINT", x0, 380.0f, 20.0f, theme::kAccent);
    DrawSensSummary(x0, 410.0f, 900.0f, cfg_.dpi, cfg_.sens);
    Text(TextFormat("Range round 1: %.3f  vs  %.3f", cfg_.sens * 0.5, cfg_.sens * 1.5), x0, 520.0f, 22.0f, theme::kText);

    if (Button(Rectangle{x0, 580.0f, 380.0f, 66.0f}, "START NEW SESSION", true)) {
        finder_.Start(cfg_.sens, cfg_.dpi, static_cast<unsigned int>(rng_.Int(0, 0xFFFF)));
        finderSaved_ = false;
        screen_ = Screen::FinderReady;
        return;
    }

    // Past sessions + combined recommendation.
    const float rx = x0 + 960.0f, rw = kContentWidth - 960.0f;
    Angled(Rectangle{rx, 130.0f, rw, 560.0f}, theme::kPanel, 16.0f);
    Text("SAVED SESSIONS", rx + 24.0f, 150.0f, 20.0f, theme::kAccent);
    float sy = 186.0f;
    int shown = 0;
    for (auto it = finderSessions_.rbegin(); it != finderSessions_.rend() && shown < 8; ++it, ++shown) {
        Text(TextFormat("%s   %.3f @ %.0f DPI   %.1f cm", it->timestamp.substr(0, 10).c_str(), it->recommended, it->dpi,
                        val::Cm360(it->dpi, it->recommended)),
             rx + 24.0f, sy, 18.0f, theme::kText);
        sy += 28.0f;
    }
    if (finderSessions_.empty()) Text("No sessions yet.", rx + 24.0f, sy, 18.0f, theme::kTextDim);

    const double combined = CombinedRecommendation(finderSessions_, cfg_.dpi);
    if (combined > 0.0) {
        Text(TextFormat("COMBINED (%d sessions, averaged in cm/360)", static_cast<int>(finderSessions_.size())), rx + 24.0f,
             440.0f, 18.0f, theme::kTextDim);
        Text(TextFormat("%.3f", combined), rx + 24.0f, 466.0f, 48.0f, theme::kText);
        Text(TextFormat("eDPI %.0f  |  %.1f cm/360", val::Edpi(cfg_.dpi, combined), val::Cm360(cfg_.dpi, combined)),
             rx + 24.0f, 524.0f, 20.0f, theme::kTextDim);
        if (Button(Rectangle{rx + 24.0f, 610.0f, rw - 48.0f, 56.0f}, "APPLY COMBINED")) SetSens(combined);
    }

    if (Button(Rectangle{x0, VH() - 100.0f, 260.0f, 60.0f}, "BACK") || IsKeyPressed(KEY_ESCAPE)) {
        screen_ = Screen::MainMenu;
    }
}

void App::ScreenFinderReady() {
    const float x0 = ContentX();
    Title(TextFormat("ROUND %d / %d", finder_.Round(), SensFinder::kRounds), x0, 60.0f, 52.0f);

    // Round progress bar.
    const float bw = kContentWidth / SensFinder::kRounds;
    for (int i = 0; i < SensFinder::kRounds; ++i) {
        const Color c = i + 1 < finder_.Round() ? theme::kAccent : (i + 1 == finder_.Round() ? theme::kText : theme::kPanel2);
        Fill(Rectangle{x0 + static_cast<float>(i) * bw, 140.0f, bw - 6.0f, 8.0f}, c);
    }

    Text(TextFormat("TEST %c", finder_.CurrentLabel()), x0, 200.0f, 110.0f, theme::kAccent);
    TextBlock("20 seconds: 7 s of flicks, 7 s of tracking (hold fire on the strafing agent), then 6 s of small "
              "micro-adjustment targets. The sensitivity is hidden until the round ends so you judge it by feel and "
              "results only. Play normally - don't try to 'test' the sens.",
              x0, 350.0f, 900.0f, 22.0f, theme::kTextDim);

    if (Button(Rectangle{x0, 520.0f, 340.0f, 70.0f}, "START TEST", true) || IsKeyPressed(KEY_ENTER) ||
        IsKeyPressed(KEY_SPACE)) {
        StartRun(ModeId::Mixed, true);
        return;
    }
    if (Button(Rectangle{x0 + 360.0f, 520.0f, 260.0f, 70.0f}, "ABORT") || IsKeyPressed(KEY_ESCAPE)) {
        finder_.Cancel();
        screen_ = Screen::FinderIntro;
    }
}

void App::ScreenFinderComfort() {
    const float x0 = ContentX();
    const RunStats& s = finderLastStats_;
    Title(TextFormat("HOW DID TEST %c FEEL?", finder_.CurrentLabel()), x0, 60.0f, 52.0f);

    const float tw = (kContentWidth - 30.0f) / 4.0f;
    StatTile(Rectangle{x0, 170.0f, tw, 96.0f}, "ACCURACY", Fmt(s.shots > 0 ? 100.0 * s.hits / s.shots : 0.0, 1) + "%",
             theme::kAccent);
    StatTile(Rectangle{x0 + tw + 10.0f, 170.0f, tw, 96.0f}, "AVG TTK", MsOrDash(s.AvgTtkMs()), theme::kText);
    StatTile(Rectangle{x0 + 2.0f * (tw + 10.0f), 170.0f, tw, 96.0f}, "TRACKING",
             Fmt(std::max(0.0, s.TrackingPct()) * 100.0, 1) + "%", theme::kText);
    StatTile(Rectangle{x0 + 3.0f * (tw + 10.0f), 170.0f, tw, 96.0f}, "OVER / UNDER",
             TextFormat("%d / %d", s.overshoots, s.undershoots), theme::kText);

    Text("Rate the comfort of this sensitivity (keys 1-5 work too):", x0, 320.0f, 24.0f, theme::kText);
    static const char* const labels[] = {"1  VERY BAD", "2  UNCOMFORTABLE", "3  OK", "4  COMFORTABLE", "5  PERFECT"};
    int picked = 0;
    const float bw = (kContentWidth - 40.0f) / 5.0f;
    for (int i = 0; i < 5; ++i) {
        if (Button(Rectangle{x0 + static_cast<float>(i) * (bw + 10.0f), 370.0f, bw, 90.0f}, labels[i], i == 2)) picked = i + 1;
        if (IsKeyPressed(KEY_ONE + i) || IsKeyPressed(KEY_KP_1 + i)) picked = i + 1;
    }
    if (picked > 0) {
        const bool roundDone = finder_.Submit(s, picked);
        finderRun_ = false;
        screen_ = roundDone ? Screen::FinderRound : Screen::FinderReady;
    }
}

void App::ScreenFinderRound() {
    const float x0 = ContentX();
    const int round = finder_.Finished() ? SensFinder::kRounds : finder_.Round() - 1;
    Title(TextFormat("ROUND %d RESULT", round), x0, 60.0f, 52.0f);

    const FinderTest* hi = finder_.LastRoundTest(true);
    const FinderTest* lo = finder_.LastRoundTest(false);
    const bool highWon = finder_.LastRoundHighWon();
    auto panel = [&](float x, const FinderTest* t, bool won) {
        if (!t) return;
        const Rectangle r = {x, 160.0f, 720.0f, 380.0f};
        Angled(r, won ? theme::kPanel2 : theme::kPanel, 16.0f);
        if (won) Fill(Rectangle{r.x, r.y, 6.0f, r.height - 16.0f}, theme::kAccent);
        Text(TextFormat("TEST %c  -  SENS %.3f", t->label, t->sens), r.x + 30.0f, r.y + 24.0f, 32.0f, theme::kText);
        Text(won ? "WINNER" : "", r.x + r.width - 30.0f, r.y + 30.0f, 24.0f, theme::kAccent, Align::Right);
        Text(TextFormat("SCORE %.1f", t->score.total), r.x + 30.0f, r.y + 80.0f, 44.0f, won ? theme::kAccent : theme::kText);
        const char* const names[] = {"Accuracy", "Speed (TTK)", "Precision", "Tracking", "Comfort"};
        const double vals[] = {t->score.accuracy, t->score.speed, t->score.precision, t->score.tracking, t->score.comfort};
        for (int i = 0; i < 5; ++i) {
            const float yy = r.y + 150.0f + static_cast<float>(i) * 42.0f;
            Text(names[i], r.x + 30.0f, yy, 20.0f, theme::kTextDim);
            Fill(Rectangle{r.x + 220.0f, yy + 6.0f, 380.0f, 12.0f}, theme::kBg);
            Fill(Rectangle{r.x + 220.0f, yy + 6.0f, 380.0f * static_cast<float>(vals[i]), 12.0f}, theme::kAccentDim);
            Text(Fmt(vals[i] * 100.0, 0), r.x + 690.0f, yy, 20.0f, theme::kText, Align::Right);
        }
    };
    panel(x0, lo, !highWon);
    panel(x0 + 780.0f, hi, highWon);
    Text("LOWER SENS", x0, 548.0f, 18.0f, theme::kTextDim);
    Text("HIGHER SENS", x0 + 780.0f, 548.0f, 18.0f, theme::kTextDim);

    Text(TextFormat("Range narrows toward the %s side. New range: %.3f - %.3f", highWon ? "higher" : "lower", finder_.Low(),
                    finder_.High()),
         x0, 610.0f, 24.0f, theme::kText);

    if (Button(Rectangle{x0, 680.0f, 340.0f, 70.0f}, finder_.Finished() ? "SEE RESULT" : "NEXT ROUND", true) ||
        IsKeyPressed(KEY_ENTER)) {
        screen_ = finder_.Finished() ? Screen::FinderFinal : Screen::FinderReady;
    }
}

void App::ScreenFinderFinal() {
    if (!finderSaved_) {
        finder_.Save(DataDir(), &finderLastSession_);
        finderSessions_ = LoadFinderSessions(DataDir());
        finderSaved_ = true;
    }
    const float x0 = ContentX();
    Title("RECOMMENDED SENSITIVITY", x0, 50.0f, 52.0f);

    const double rec = std::round(finder_.Recommended() * 1000.0) / 1000.0;
    const double dpi = finder_.Dpi();
    const float tw = (kContentWidth - 30.0f) / 4.0f;
    StatTile(Rectangle{x0, 140.0f, tw, 100.0f}, "VALORANT SENS", Fmt(rec, 3), theme::kAccent);
    StatTile(Rectangle{x0 + tw + 10.0f, 140.0f, tw, 100.0f}, "eDPI", Fmt(val::Edpi(dpi, rec), 0), theme::kAccent);
    StatTile(Rectangle{x0 + 2.0f * (tw + 10.0f), 140.0f, tw, 100.0f}, "CM / 360", Fmt(val::Cm360(dpi, rec), 1), theme::kAccent);
    const double change = (rec / finder_.StartSens() - 1.0) * 100.0;
    StatTile(Rectangle{x0 + 3.0f * (tw + 10.0f), 140.0f, tw, 100.0f}, "VS START",
             TextFormat("%+.1f%%  (was %.3f)", change, finder_.StartSens()), theme::kText);

    std::vector<ChartPoint> pts;
    for (const FinderTest& t : finder_.Tests()) pts.push_back(ChartPoint{static_cast<float>(t.sens), static_cast<float>(t.score.total)});
    ScatterChart(Rectangle{x0 + 60.0f, 300.0f, 860.0f, 380.0f}, pts, theme::kAccent, static_cast<float>(rec),
                 "SENSITIVITY (green line = recommendation)", "TEST SCORE");

    const float rx = x0 + 980.0f, rw = kContentWidth - 980.0f;
    Angled(Rectangle{rx, 280.0f, rw, 420.0f}, theme::kPanel, 16.0f);
    const double combined = CombinedRecommendation(finderSessions_, cfg_.dpi);
    Text(TextFormat("ALL SESSIONS (%d)", static_cast<int>(finderSessions_.size())), rx + 24.0f, 300.0f, 20.0f, theme::kAccent);
    Text(TextFormat("%.3f", combined), rx + 24.0f, 334.0f, 52.0f, theme::kText);
    Text(TextFormat("eDPI %.0f  |  %.1f cm/360 @ %.0f DPI", val::Edpi(cfg_.dpi, combined), val::Cm360(cfg_.dpi, combined),
                    cfg_.dpi),
         rx + 24.0f, 398.0f, 18.0f, theme::kTextDim);
    TextBlock("Averaging several sessions from different days removes day-to-day noise and gives the most reliable result.",
              rx + 24.0f, 440.0f, rw - 48.0f, 18.0f, theme::kTextDim);

    const bool appliedThis = std::fabs(cfg_.sens - rec) < 1e-9;
    if (Button(Rectangle{x0, 730.0f, 380.0f, 68.0f}, appliedThis ? "APPLIED" : "APPLY THIS RESULT", true, !appliedThis)) {
        SetSens(rec);
    }
    if (Button(Rectangle{rx + 24.0f, 610.0f, rw - 48.0f, 60.0f}, "APPLY COMBINED", false, combined > 0.0)) SetSens(combined);
    if (Button(Rectangle{x0 + 400.0f, 730.0f, 260.0f, 68.0f}, "MAIN MENU") || IsKeyPressed(KEY_ESCAPE)) {
        screen_ = Screen::MainMenu;
        return;
    }
    Text("Remember to set the same value in Valorant. Saved to finder_sessions.csv / finder_tests.csv.", x0, 820.0f, 18.0f,
         theme::kTextDim);
}

// ===========================================================================
// Aim rank

const char* App::RankComment(const AimRank& r, unsigned int seed) const {
    return cfg_.roastMode ? RankRoast(r, seed) : RankDescription(r);
}

void App::ScreenRank() {
    const float x0 = ContentX();
    Title("AIM RANK", x0, 50.0f, 52.0f);
    Text("Estimated from your trainer stats. Aim only - real rank also depends on game sense, utility and teamwork.",
         x0 + 6.0f, 118.0f, 18.0f, theme::kTextDim);

    // Overall rank, big.
    const Rectangle big = {x0, 160.0f, 620.0f, 540.0f};
    AimRank overall;
    int modesUsed = 0;
    const bool hasOverall = OverallRank(stats_, overall, &modesUsed);
    Angled(big, theme::kPanel, 20.0f);
    if (hasOverall) {
        Fill(Rectangle{big.x, big.y, 6.0f, big.height - 20.0f}, TierColor(overall.tier));
        DrawRankBadge(big.x + big.width * 0.5f, big.y + 170.0f, 230.0f, overall);
        RankText(overall, big.x + big.width * 0.5f, big.y + 318.0f, 54.0f, Align::Center);
        if (overall.tier < kTierCount - 1) {
            const Rectangle bar = {big.x + 110.0f, big.y + 392.0f, big.width - 220.0f, 8.0f};
            Fill(bar, theme::kBg);
            Fill(Rectangle{bar.x, bar.y, bar.width * static_cast<float>(overall.progress), bar.height}, TierColor(overall.tier));
            const AimRank next = RankFromPoints(std::floor(overall.points) + 1.0);
            Text(TextFormat("%.0f%% to %s", overall.progress * 100.0, RankLabel(next).c_str()), big.x + big.width * 0.5f,
                 bar.y + 14.0f, 16.0f, theme::kTextDim, Align::Center);
        }
        TextBlock(RankComment(overall, static_cast<unsigned int>(overall.points * 100.0)), big.x + 40.0f, big.y + 440.0f,
                  big.width - 80.0f, 22.0f, theme::kText);
    } else {
        AimRank unranked;
        DrawRankBadge(big.x + big.width * 0.5f, big.y + 170.0f, 230.0f, unranked, 0.25f, false);
        TextBold("UNRANKED", big.x + big.width * 0.5f, big.y + 318.0f, 50.0f, theme::kTextDim, Align::Center);
        TextBlock(TextFormat("Play at least %d different modes (runs of 20 s or longer) to get your overall rank. "
                             "Modes ranked so far: %d.", kMinModesForOverall, modesUsed),
                  big.x + 40.0f, big.y + 400.0f, big.width - 80.0f, 20.0f, theme::kTextDim);
    }

    // Per-mode ranks.
    const float mx = x0 + 660.0f, mw = kContentWidth - 660.0f;
    for (int i = 0; i < kPlayableModeCount; ++i) {
        const ModeId m = static_cast<ModeId>(i);
        const Rectangle row = {mx, 160.0f + static_cast<float>(i) * 90.0f, mw, 80.0f};
        Angled(row, theme::kPanel, 12.0f);
        AimRank mr;
        int ranked = 0;
        const bool has = ModeRank(stats_, m, mr, &ranked);
        if (has) {
            DrawRankBadge(row.x + 46.0f, row.y + 34.0f, 54.0f, mr);
        } else {
            DrawRankBadge(row.x + 46.0f, row.y + 34.0f, 54.0f, AimRank{}, 0.2f, false);
        }
        TextBold(ModeName(m), row.x + 92.0f, row.y + 14.0f, 24.0f, theme::kText);
        Text(has ? TextFormat("%d ranked runs", ranked) : "Not ranked yet - play a 20 s+ run", row.x + 92.0f, row.y + 46.0f,
             16.0f, theme::kTextDim);
        if (has) {
            RankText(mr, row.x + row.width - 24.0f, row.y + 24.0f, 30.0f, Align::Right);
        }
    }

    // Tier ladder.
    const float ly = 760.0f;
    const float step = kContentWidth / static_cast<float>(kTierCount);
    for (int t = 0; t < kTierCount; ++t) {
        AimRank tr;
        tr.tier = t;
        tr.division = 3;
        const bool current = hasOverall && overall.tier == t;
        const float cx = x0 + step * (static_cast<float>(t) + 0.5f);
        if (current) Angled(Rectangle{cx - step * 0.5f + 6.0f, ly - 10.0f, step - 12.0f, 150.0f}, theme::kPanel2, 12.0f);
        DrawRankBadge(cx, ly + 50.0f, 74.0f, tr, current || !hasOverall ? 1.0f : 0.45f, false);
        Text(TierName(t), cx, ly + 104.0f, 18.0f, current ? TierColor(t) : theme::kTextDim, Align::Center);
    }

    if (Button(Rectangle{x0, VH() - 100.0f, 260.0f, 60.0f}, "BACK", true) || IsKeyPressed(KEY_ESCAPE)) {
        screen_ = Screen::MainMenu;
    }
    Text(cfg_.roastMode ? "Roast mode is on (Settings > Gameplay)." : "Roast mode is off (Settings > Gameplay).", x0 + 290.0f,
         VH() - 80.0f, 18.0f, theme::kTextDim);
}
