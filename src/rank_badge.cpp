// rank_badge.cpp - one distinct emblem per tier:
//   Iron      plain shield, one chevron
//   Bronze    shield, chevron + bar
//   Silver    shield with a double rim, two chevrons
//   Gold      shield with wings, two chevrons
//   Platinum  diamond-shaped plate with a gem
//   Diamond   faceted cut gem
//   Ascendant shield with swept wings and an up arrow
//   Immortal  crowned shield with three chevrons
//   Radiant   starburst with a glowing core
#include "rank_badge.h"

#include <cmath>

#include "ui.h"

using namespace ui;

namespace {

constexpr float kPi = 3.14159265f;

Color Mul(Color c, float k, float alpha) {
    auto ch = [k](unsigned char v) {
        const float x = static_cast<float>(v) * k;
        return static_cast<unsigned char>(x > 255.0f ? 255.0f : x);
    };
    return Color{ch(c.r), ch(c.g), ch(c.b), static_cast<unsigned char>(static_cast<float>(c.a) * alpha)};
}

// Convex polygon fill (fan from the centroid).
void Poly(const Vector2* p, int n, Color c) {
    Vector2 m = {0.0f, 0.0f};
    for (int i = 0; i < n; ++i) {
        m.x += p[i].x;
        m.y += p[i].y;
    }
    m.x /= static_cast<float>(n);
    m.y /= static_cast<float>(n);
    for (int i = 0; i < n; ++i) Tri(m, p[i], p[(i + 1) % n], c);
}

// Shield: flat-ish top, pointed bottom (6 points).
void Shield(float cx, float cy, float R, Color c) {
    const Vector2 p[6] = {{cx - R * 0.78f, cy - R * 0.80f}, {cx, cy - R * 1.0f},         {cx + R * 0.78f, cy - R * 0.80f},
                          {cx + R * 0.78f, cy + R * 0.20f}, {cx, cy + R * 1.0f},         {cx - R * 0.78f, cy + R * 0.20f}};
    Poly(p, 6, c);
}

void Chevron(float cx, float cy, float w, float h, float t, Color c) {
    // Upward pointing chevron: two slanted bars.
    Tri(Vector2{cx - w, cy + h}, Vector2{cx, cy}, Vector2{cx, cy + t}, c);
    Tri(Vector2{cx - w, cy + h}, Vector2{cx, cy + t}, Vector2{cx - w, cy + h + t}, c);
    Tri(Vector2{cx + w, cy + h}, Vector2{cx, cy + t}, Vector2{cx, cy}, c);
    Tri(Vector2{cx + w, cy + h}, Vector2{cx + w, cy + h + t}, Vector2{cx, cy + t}, c);
}

void Wings(float cx, float cy, float R, Color c, bool swept) {
    for (int s = -1; s <= 1; s += 2) {
        const float sx = static_cast<float>(s);
        const float lift = swept ? -R * 0.35f : 0.0f;
        for (int i = 0; i < 3; ++i) {
            const float fi = static_cast<float>(i);
            const Vector2 a = {cx + sx * R * 0.72f, cy - R * 0.45f + fi * R * 0.28f};
            const Vector2 b = {cx + sx * (R * 1.30f - fi * R * 0.12f), cy - R * 0.62f + fi * R * 0.30f + lift * (1.0f - fi * 0.3f)};
            const Vector2 d = {cx + sx * R * 0.72f, cy - R * 0.28f + fi * R * 0.28f};
            Tri(a, b, d, c);
        }
    }
}

}  // namespace

void DrawRankBadge(float cx, float cy, float size, const AimRank& r, float alpha, bool showDivision) {
    const int tier = r.tier;
    const Color base = TierColor(tier);
    const Color c = Mul(base, 1.0f, alpha);
    const Color light = Mul(base, 1.25f, alpha);
    const Color deep = Mul(base, 0.55f, alpha);
    const Color dark = Color{18, 21, 28, static_cast<unsigned char>(255.0f * alpha)};
    const float R = size * 0.42f;

    switch (tier) {
        case 0:  // Iron
        case 1:  // Bronze
        case 2:  // Silver
        case 3:  // Gold
        case 6:  // Ascendant
        case 7:  // Immortal
        {
            if (tier == 3) Wings(cx, cy, R, deep, false);
            if (tier == 6) Wings(cx, cy, R, c, true);
            if (tier == 7) {
                // Crown spikes above the shield.
                for (int i = -1; i <= 1; ++i) {
                    const float fx = static_cast<float>(i);
                    const float h = (i == 0) ? R * 0.55f : R * 0.38f;
                    Tri(Vector2{cx + fx * R * 0.48f - R * 0.2f, cy - R * 0.82f}, Vector2{cx + fx * R * 0.48f, cy - R * 0.82f - h},
                        Vector2{cx + fx * R * 0.48f + R * 0.2f, cy - R * 0.82f}, light);
                }
            }
            Shield(cx, cy, R, c);
            Shield(cx, cy, R * 0.80f, dark);
            if (tier == 2 || tier == 7) {
                Shield(cx, cy, R * 0.68f, deep);  // double rim
                Shield(cx, cy, R * 0.60f, dark);
            }
            const float w = R * 0.42f, h = R * 0.30f, t = R * 0.16f;
            if (tier == 0) {
                Chevron(cx, cy - R * 0.12f, w, h, t, c);
            } else if (tier == 1) {
                Chevron(cx, cy - R * 0.26f, w, h, t, c);
                Fill(Rectangle{cx - w * 0.8f, cy + R * 0.22f, w * 1.6f, t}, c);
            } else if (tier == 6) {
                // Up arrow.
                Tri(Vector2{cx, cy - R * 0.55f}, Vector2{cx - R * 0.36f, cy - R * 0.1f}, Vector2{cx + R * 0.36f, cy - R * 0.1f}, light);
                Fill(Rectangle{cx - R * 0.12f, cy - R * 0.12f, R * 0.24f, R * 0.55f}, light);
            } else {
                const int n = (tier == 7) ? 3 : 2;
                for (int i = 0; i < n; ++i) Chevron(cx, cy - R * 0.34f + static_cast<float>(i) * R * 0.26f, w, h, t, i == 0 ? light : c);
            }
            break;
        }
        case 4: {  // Platinum: diamond plate with a gem
            const Vector2 outer[4] = {{cx, cy - R}, {cx + R * 0.9f, cy}, {cx, cy + R}, {cx - R * 0.9f, cy}};
            Poly(outer, 4, c);
            const Vector2 inner[4] = {{cx, cy - R * 0.78f}, {cx + R * 0.7f, cy}, {cx, cy + R * 0.78f}, {cx - R * 0.7f, cy}};
            Poly(inner, 4, dark);
            const Vector2 gem[4] = {{cx, cy - R * 0.42f}, {cx + R * 0.34f, cy}, {cx, cy + R * 0.42f}, {cx - R * 0.34f, cy}};
            Poly(gem, 4, light);
            Tri(Vector2{cx, cy - R * 0.42f}, Vector2{cx, cy + R * 0.42f}, Vector2{cx - R * 0.34f, cy}, c);
            break;
        }
        case 5: {  // Diamond: faceted cut gem
            const Vector2 top = {cx, cy - R * 0.1f};
            const Vector2 p[5] = {{cx - R * 0.95f, cy - R * 0.35f}, {cx - R * 0.5f, cy - R * 0.85f}, {cx + R * 0.5f, cy - R * 0.85f},
                                  {cx + R * 0.95f, cy - R * 0.35f}, {cx, cy + R * 1.0f}};
            Poly(p, 5, c);
            // Facets.
            Tri(p[0], p[1], top, light);
            Tri(p[2], p[3], top, deep);
            Tri(p[1], p[2], top, Mul(base, 1.45f, alpha));
            Tri(p[0], top, p[4], Mul(base, 0.85f, alpha));
            Tri(top, p[3], p[4], deep);
            break;
        }
        default: {  // Radiant: starburst
            const int rays = 12;
            for (int i = 0; i < rays; ++i) {
                const float a = 2.0f * kPi * static_cast<float>(i) / static_cast<float>(rays);
                const float len = (i % 2 == 0) ? R * 1.12f : R * 0.78f;
                const float wdt = 0.16f;
                Tri(Vector2{cx + std::cos(a - wdt) * R * 0.35f, cy + std::sin(a - wdt) * R * 0.35f},
                    Vector2{cx + std::cos(a) * len, cy + std::sin(a) * len},
                    Vector2{cx + std::cos(a + wdt) * R * 0.35f, cy + std::sin(a + wdt) * R * 0.35f}, i % 2 == 0 ? light : c);
            }
            Circle(Vector2{cx, cy}, R * 0.46f, c);
            Circle(Vector2{cx, cy}, R * 0.34f, Color{255, 250, 225, static_cast<unsigned char>(255.0f * alpha)});
            CircleLines(Vector2{cx, cy}, R * 1.2f, 2.0f, Mul(base, 1.0f, alpha * 0.5f));
            break;
        }
    }

    // Division pips (Radiant has none).
    if (showDivision && tier < kTierCount - 1) {
        const float pw = size * 0.14f, ph = size * 0.055f, gap = size * 0.05f;
        const float total = 3.0f * pw + 2.0f * gap;
        for (int i = 0; i < 3; ++i) {
            const Rectangle pip = {cx - total * 0.5f + static_cast<float>(i) * (pw + gap), cy + R + size * 0.1f, pw, ph};
            Fill(pip, i < r.division ? c : Mul(base, 1.0f, alpha * 0.25f));
        }
    }
}
