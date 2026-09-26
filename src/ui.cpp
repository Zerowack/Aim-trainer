// ui.cpp - immediate-mode widgets, fonts and charts.
#include "ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <unordered_map>

namespace ui {

namespace theme {
const Color kBg = {15, 17, 23, 255};
const Color kPanel = {24, 28, 36, 245};
const Color kPanel2 = {36, 41, 52, 255};
const Color kLine = {60, 66, 80, 255};
const Color kAccent = {255, 75, 87, 255};
const Color kAccentDim = {150, 45, 55, 255};
const Color kText = {236, 232, 225, 255};
const Color kTextDim = {150, 154, 164, 255};
const Color kGood = {90, 220, 160, 255};
const Color kWarn = {255, 196, 80, 255};
}  // namespace theme

namespace {

Font g_font = {};
bool g_customFont = false;
float g_scale = 1.0f;
float g_vw = 1600.0f;
float g_vh = 1080.0f;

int g_focusedBox = -1;          // TextBox id with keyboard focus
bool g_selectAll = false;       // focused box has "everything selected"
const void* g_activeSlider = nullptr;

// Hover animation state, keyed by widget position.
std::unordered_map<uint64_t, float> g_hoverAnim;

constexpr float kBaseW = 1600.0f;
constexpr float kBaseH = 1080.0f;

float Px(float v) { return v * g_scale; }

bool LeftPressed() { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT); }

Color Lerp(Color a, Color b, float k) {
    auto l = [k](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * k);
    };
    return Color{l(a.r, b.r), l(a.g, b.g), l(a.b, b.b), l(a.a, b.a)};
}

std::string FormatValue(const char* fmt, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), fmt, v);
    return buf;
}

}  // namespace

void Init() {
    // Use a clean Windows system font (not a game font). Falls back to
    // raylib's built-in font if none is found.
    const char* const candidates[] = {"C:/Windows/Fonts/bahnschrift.ttf", "C:/Windows/Fonts/segoeui.ttf",
                                      "C:/Windows/Fonts/arial.ttf"};
    for (const char* path : candidates) {
        if (!FileExists(path)) continue;
        Font f = LoadFontEx(path, 64, nullptr, 0);
        if (f.texture.id != 0 && f.glyphCount > 0) {
            g_font = f;
            g_customFont = true;
            GenTextureMipmaps(&g_font.texture);
            SetTextureFilter(g_font.texture, TEXTURE_FILTER_TRILINEAR);
            break;
        }
    }
    if (!g_customFont) g_font = GetFontDefault();
}

void Shutdown() {
    if (g_customFont) UnloadFont(g_font);
    g_customFont = false;
}

void BeginFrame() {
    const float w = static_cast<float>(std::max(1, GetScreenWidth()));
    const float h = static_cast<float>(std::max(1, GetScreenHeight()));
    g_scale = std::min(w / kBaseW, h / kBaseH);
    g_vw = w / g_scale;
    g_vh = h / g_scale;
    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) g_activeSlider = nullptr;
}

float VW() { return g_vw; }
float VH() { return g_vh; }
float Scale() { return g_scale; }

Rectangle ToScreen(Rectangle r) { return Rectangle{Px(r.x), Px(r.y), Px(r.width), Px(r.height)}; }
Vector2 ToScreen(Vector2 p) { return Vector2{Px(p.x), Px(p.y)}; }

Vector2 Mouse() {
    const Vector2 m = GetMousePosition();
    return Vector2{m.x / g_scale, m.y / g_scale};
}

bool Hover(Rectangle r) { return CheckCollisionPointRec(Mouse(), r); }

Color Alpha(Color c, float alpha) {
    return Color{c.r, c.g, c.b, static_cast<unsigned char>(std::clamp(alpha, 0.0f, 1.0f) * static_cast<float>(c.a))};
}

// ---------------------------------------------------------------------------
// Drawing

float TextWidth(const std::string& s, float size) {
    const float px = Px(size);
    return MeasureTextEx(g_font, s.c_str(), px, px * 0.02f).x / g_scale;
}

void Text(const std::string& s, float x, float y, float size, Color c, Align a) {
    const float px = Px(size);
    const float spacing = px * 0.02f;
    float sx = Px(x);
    if (a != Align::Left) {
        const float w = MeasureTextEx(g_font, s.c_str(), px, spacing).x;
        sx -= (a == Align::Center) ? w * 0.5f : w;
    }
    DrawTextEx(g_font, s.c_str(), Vector2{std::round(sx), std::round(Px(y))}, px, spacing, c);
}

void Fill(Rectangle r, Color c) { DrawRectangleRec(ToScreen(r), c); }

void Border(Rectangle r, float thickness, Color c) {
    DrawRectangleLinesEx(ToScreen(r), std::max(1.0f, Px(thickness)), c);
}

void Tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    Vector2 sa = ToScreen(a), sb = ToScreen(b), sc = ToScreen(c);
    // raylib wants counter-clockwise (on screen) winding; fix it if needed.
    const float cross = (sb.x - sa.x) * (sc.y - sa.y) - (sb.y - sa.y) * (sc.x - sa.x);
    if (cross > 0.0f) std::swap(sb, sc);
    DrawTriangle(sa, sb, sc, col);
}

void Angled(Rectangle r, Color c, float cut) {
    cut = std::min(cut, std::min(r.width, r.height) * 0.5f);
    const Vector2 p[6] = {{r.x, r.y},
                          {r.x + r.width - cut, r.y},
                          {r.x + r.width, r.y + cut},
                          {r.x + r.width, r.y + r.height},
                          {r.x + cut, r.y + r.height},
                          {r.x, r.y + r.height - cut}};
    const Vector2 center = {r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    for (int i = 0; i < 6; ++i) Tri(center, p[i], p[(i + 1) % 6], c);
}

void Line(Vector2 a, Vector2 b, float thickness, Color c) {
    DrawLineEx(ToScreen(a), ToScreen(b), std::max(1.0f, Px(thickness)), c);
}

void Title(const std::string& s, float x, float y, float size) {
    // Slanted red bar in front of the title.
    const float h = size * 0.9f;
    Tri(Vector2{x, y + h}, Vector2{x + h * 0.35f, y + 2.0f}, Vector2{x + h * 0.55f, y + 2.0f}, theme::kAccent);
    Tri(Vector2{x, y + h}, Vector2{x + h * 0.55f, y + 2.0f}, Vector2{x + h * 0.2f, y + h}, theme::kAccent);
    TextBold(s, x + h * 0.75f, y, size, theme::kText);
}

void TextBold(const std::string& s, float x, float y, float size, Color c, Align a) {
    // Faux bold: the Windows UI fonts ship as a single weight here.
    const float off = std::max(1.0f / g_scale, size * 0.035f);
    Text(s, x, y, size, c, a);
    Text(s, x + off, y, size, c, a);
}

void Circle(Vector2 center, float radius, Color c) { DrawCircleV(ToScreen(center), Px(radius), c); }

void CircleLines(Vector2 center, float radius, float thickness, Color c) {
    const Vector2 s = ToScreen(center);
    DrawRing(s, Px(radius) - std::max(1.0f, Px(thickness)), Px(radius), 0.0f, 360.0f, 48, c);
}

void Logo(float x, float y, float size) {
    // Mirrors assets/icon.svg (designed on a 512 grid).
    const float k = size / 512.0f;
    auto P = [&](float px, float py) { return Vector2{x + px * k, y + py * k}; };
    auto R = [&](float px, float py, float w, float h) { return Rectangle{x + px * k, y + py * k, w * k, h * k}; };
    // Tile with cut corners + red border.
    const Rectangle tile = R(16, 16, 480, 480);
    Angled(tile, theme::kAccent, 84.0f * k);
    Angled(Rectangle{tile.x + 10.0f * k, tile.y + 10.0f * k, tile.width - 20.0f * k, tile.height - 20.0f * k},
           Color{24, 28, 37, 255}, 80.0f * k);
    // Slash.
    const Color slash = {190, 52, 64, 255};
    Tri(P(118, 440), P(196, 440), P(394, 72), slash);
    Tri(P(118, 440), P(394, 72), P(316, 72), slash);
    // Crosshair with dark outline.
    const Color dark = {13, 15, 20, 255};
    Fill(R(220, 62, 72, 140), dark);
    Fill(R(220, 310, 72, 140), dark);
    Fill(R(62, 220, 140, 72), dark);
    Fill(R(310, 220, 140, 72), dark);
    Fill(R(216, 216, 80, 80), dark);
    Fill(R(232, 74, 48, 116), theme::kText);
    Fill(R(232, 322, 48, 116), theme::kText);
    Fill(R(74, 232, 116, 48), theme::kText);
    Fill(R(322, 232, 116, 48), theme::kText);
    Fill(R(228, 228, 56, 56), theme::kAccent);
}

float HoverAnim(Rectangle r, bool enabled) {
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(std::lround(r.x))) << 32) ^
                         static_cast<uint64_t>(static_cast<uint32_t>(std::lround(r.y * 3.0f + r.width)));
    float& v = g_hoverAnim[key];
    const float target = (enabled && Hover(r)) ? 1.0f : 0.0f;
    // Highlight instantly (any delay reads as input lag), fade out quickly.
    if (target > v) v = target;
    else v += (target - v) * std::min(1.0f, GetFrameTime() * 20.0f);
    if (g_hoverAnim.size() > 4096) g_hoverAnim.clear();  // screen layouts change; keep it small
    return v;
}

void Backdrop() {
    ClearBackground(theme::kBg);
    const float w = VW(), h = VH();
    // Soft vertical gradient: slightly lighter at the top.
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), Color{23, 27, 36, 255}, Color{11, 12, 16, 255});
    // Faint diagonal stripes for a bit of texture.
    const Color stripe = Alpha(theme::kAccent, 0.05f);
    for (int i = 0; i < 6; ++i) {
        const float x = w * 0.62f + static_cast<float>(i) * 120.0f;
        Tri(Vector2{x, 0.0f}, Vector2{x + 40.0f, 0.0f}, Vector2{x - h * 0.35f + 40.0f, h}, stripe);
        Tri(Vector2{x, 0.0f}, Vector2{x - h * 0.35f + 40.0f, h}, Vector2{x - h * 0.35f, h}, stripe);
    }
    Fill(Rectangle{0.0f, 0.0f, w, 4.0f}, theme::kAccent);
}

// ---------------------------------------------------------------------------
// Widgets

bool Button(Rectangle r, const std::string& label, bool primary, bool enabled) {
    const bool hover = enabled && Hover(r);
    const float a = HoverAnim(r, enabled);
    Color fill = primary ? theme::kAccent : theme::kPanel2;
    if (!enabled) fill = Alpha(theme::kPanel2, 0.5f);
    else fill = primary ? Lerp(theme::kAccent, WHITE, 0.16f * a) : Lerp(theme::kPanel2, theme::kLine, 0.65f * a);
    Angled(r, fill, 10.0f);
    // Red underline slides in from the left on hover.
    if (!primary && a > 0.01f) Fill(Rectangle{r.x, r.y + r.height - 3.0f, (r.width - 10.0f) * a, 3.0f}, theme::kAccent);
    if (primary && a > 0.01f) Border(Rectangle{r.x, r.y, r.width, r.height}, 1.0f, Alpha(WHITE, 0.25f * a));
    const float size = std::min(26.0f, r.height * 0.5f);
    Text(label, r.x + r.width * 0.5f, r.y + (r.height - size) * 0.5f, size,
         enabled ? theme::kText : theme::kTextDim, Align::Center);
    return hover && LeftPressed();
}

bool Tab(Rectangle r, const std::string& label, bool active) {
    const bool hover = Hover(r);
    if (active) Fill(r, theme::kPanel2);
    const float size = std::min(24.0f, r.height * 0.5f);
    Text(label, r.x + r.width * 0.5f, r.y + (r.height - size) * 0.5f, size,
         active ? theme::kText : (hover ? theme::kText : theme::kTextDim), Align::Center);
    if (active) Fill(Rectangle{r.x, r.y + r.height - 3.0f, r.width, 3.0f}, theme::kAccent);
    return hover && LeftPressed();
}

bool Toggle(Rectangle r, const std::string& label, bool* value) {
    const bool hover = Hover(r);
    Text(label, r.x, r.y + (r.height - 22.0f) * 0.5f, 22.0f, theme::kText);
    const Rectangle box = {r.x + r.width - 64.0f, r.y + (r.height - 28.0f) * 0.5f, 64.0f, 28.0f};
    Fill(box, *value ? theme::kAccent : theme::kPanel2);
    const Rectangle knob = {*value ? box.x + 36.0f : box.x + 4.0f, box.y + 4.0f, 24.0f, 20.0f};
    Fill(knob, *value ? theme::kText : theme::kTextDim);
    if (hover) Border(box, 1.0f, theme::kText);
    if (hover && LeftPressed()) {
        *value = !*value;
        return true;
    }
    return false;
}

namespace {

// Shared slider body. 'id' identifies the slider while it is being dragged.
// Returns the new 0..1 position while dragging, or a negative value.
float SliderBody(Rectangle r, const std::string& label, const std::string& valueText, float frac, const void* id) {
    Text(label, r.x, r.y, 20.0f, theme::kText);
    Text(valueText, r.x + r.width, r.y, 20.0f, theme::kTextDim, Align::Right);
    const Rectangle track = {r.x, r.y + r.height - 14.0f, r.width, 8.0f};
    const Rectangle hit = {track.x - 6.0f, track.y - 10.0f, track.width + 12.0f, track.height + 20.0f};
    frac = std::clamp(frac, 0.0f, 1.0f);
    Fill(track, theme::kPanel2);
    Fill(Rectangle{track.x, track.y, track.width * frac, track.height}, theme::kAccentDim);
    const Rectangle knob = {track.x + track.width * frac - 6.0f, track.y - 6.0f, 12.0f, 20.0f};
    Fill(knob, Hover(hit) || g_activeSlider == id ? theme::kText : theme::kAccent);

    if (Hover(hit) && LeftPressed()) g_activeSlider = id;
    if (g_activeSlider == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        return std::clamp((Mouse().x - track.x) / track.width, 0.0f, 1.0f);
    }
    return -1.0f;
}

}  // namespace

bool SliderF(Rectangle r, const std::string& label, float* value, float minV, float maxV, const char* fmt) {
    const float frac = (maxV > minV) ? (*value - minV) / (maxV - minV) : 0.0f;
    const float k = SliderBody(r, label, FormatValue(fmt, static_cast<double>(*value)), frac, value);
    if (k < 0.0f) return false;
    const float nv = minV + (maxV - minV) * k;
    if (nv == *value) return false;
    *value = nv;
    return true;
}

bool SliderI(Rectangle r, const std::string& label, int* value, int minV, int maxV) {
    const float frac = (maxV > minV) ? static_cast<float>(*value - minV) / static_cast<float>(maxV - minV) : 0.0f;
    const float k = SliderBody(r, label, std::to_string(*value), frac, value);
    if (k < 0.0f) return false;
    const int nv = minV + static_cast<int>(std::lround(static_cast<float>(maxV - minV) * k));
    if (nv == *value) return false;
    *value = nv;
    return true;
}

bool SliderU8(Rectangle r, const std::string& label, unsigned char* value) {
    const float frac = static_cast<float>(*value) / 255.0f;
    const float k = SliderBody(r, label, std::to_string(static_cast<int>(*value)), frac, value);
    if (k < 0.0f) return false;
    const auto nv = static_cast<unsigned char>(std::lround(255.0f * k));
    if (nv == *value) return false;
    *value = nv;
    return true;
}

bool Swatch(Rectangle r, Color c, bool selected) {
    const bool hover = Hover(r);
    Fill(r, Color{c.r, c.g, c.b, 255});
    if (selected) Border(r, 3.0f, theme::kText);
    else if (hover) Border(r, 2.0f, theme::kTextDim);
    return hover && LeftPressed();
}

float TextBlock(const std::string& s, float x, float y, float width, float size, Color c) {
    const float lineH = size * 1.35f;
    float cy = y;
    std::string line;
    size_t i = 0;
    auto flush = [&]() {
        Text(line, x, cy, size, c);
        cy += lineH;
        line.clear();
    };
    while (i <= s.size()) {
        // Next word (explicit newlines force a break).
        size_t j = i;
        while (j < s.size() && s[j] != ' ' && s[j] != '\n') ++j;
        const std::string word = s.substr(i, j - i);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && TextWidth(candidate, size) > width) {
            flush();
            line = word;
        } else {
            line = candidate;
        }
        if (j < s.size() && s[j] == '\n') flush();
        if (j >= s.size()) break;
        i = j + 1;
    }
    if (!line.empty()) flush();
    return cy - y;
}

bool Stepper(Rectangle r, const std::string& label, int* index, const char* const* items, int count) {
    Text(label, r.x, r.y + (r.height - 22.0f) * 0.5f, 22.0f, theme::kText);
    const float bw = 40.0f;
    const float vw = 220.0f;
    const Rectangle left = {r.x + r.width - vw - 2.0f * bw, r.y, bw, r.height};
    const Rectangle mid = {left.x + bw, r.y, vw, r.height};
    const Rectangle right = {mid.x + vw, r.y, bw, r.height};
    bool changed = false;
    if (Button(left, "<")) {
        *index = (*index + count - 1) % count;
        changed = true;
    }
    Fill(mid, theme::kPanel);
    Text(items[*index], mid.x + mid.width * 0.5f, mid.y + (mid.height - 22.0f) * 0.5f, 22.0f, theme::kText,
         Align::Center);
    if (Button(right, ">")) {
        *index = (*index + 1) % count;
        changed = true;
    }
    return changed;
}

bool TextBox(Rectangle r, int id, std::string* text, size_t maxLen, bool numeric) {
    const bool hover = Hover(r);
    bool changed = false;

    if (LeftPressed()) {
        if (hover && g_focusedBox != id) {
            // Clicking into a box selects all of it, so typing or pasting
            // replaces the old value (like most text fields).
            g_focusedBox = id;
            g_selectAll = !text->empty();
        } else if (!hover && g_focusedBox == id) {
            g_focusedBox = -1;
        }
    }

    auto accept = [numeric](int& c) {
        if (numeric && c == ',') c = '.';
        return numeric ? ((c >= '0' && c <= '9') || c == '.') : (c >= 32 && c < 127);
    };
    // First edit while everything is selected replaces the whole text.
    auto replaceSelection = [&]() {
        if (g_selectAll) {
            text->clear();
            g_selectAll = false;
            changed = true;
        }
    };

    const bool focused = g_focusedBox == id;
    if (focused) {
        const bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
        int ch = GetCharPressed();
        while (ch > 0) {
            if (accept(ch)) {
                replaceSelection();
                if (text->size() < maxLen) {
                    text->push_back(static_cast<char>(ch));
                    changed = true;
                }
            }
            ch = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE) || IsKeyPressed(KEY_DELETE)) {
            if (g_selectAll || ctrl) {
                g_selectAll = true;  // Ctrl+Backspace clears everything
                replaceSelection();
            } else if (!text->empty()) {
                text->pop_back();
                changed = true;
            }
        }
        if (ctrl && IsKeyPressed(KEY_A)) g_selectAll = !text->empty();
        if (ctrl && IsKeyPressed(KEY_V)) {
            replaceSelection();
            const char* clip = GetClipboardText();
            for (const char* p = clip; p && *p && text->size() < maxLen; ++p) {
                int c = static_cast<unsigned char>(*p);
                if (accept(c)) {
                    text->push_back(static_cast<char>(c));
                    changed = true;
                }
            }
        }
        if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_END) || IsKeyPressed(KEY_HOME)) {
            g_selectAll = false;
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) g_focusedBox = -1;
    }

    Fill(r, focused ? theme::kPanel2 : theme::kPanel);
    Border(r, focused ? 2.0f : 1.0f, focused ? theme::kAccent : (hover ? theme::kText : theme::kLine));
    const float size = std::min(24.0f, r.height * 0.55f);
    // Show the end of long strings.
    std::string shown = *text;
    while (!shown.empty() && TextWidth(shown, size) > r.width - 24.0f) shown.erase(0, 1);
    if (focused && g_selectAll && !shown.empty()) {
        Fill(Rectangle{r.x + 8.0f, r.y + 8.0f, TextWidth(shown, size) + 4.0f, r.height - 16.0f}, Alpha(theme::kAccent, 0.45f));
    }
    Text(shown, r.x + 10.0f, r.y + (r.height - size) * 0.5f, size, theme::kText);
    if (focused && !g_selectAll && std::fmod(GetTime(), 1.0) < 0.55) {
        const float cx = r.x + 12.0f + TextWidth(shown, size);
        Fill(Rectangle{cx, r.y + 8.0f, 2.0f, r.height - 16.0f}, theme::kText);
    }
    return changed;
}

void FocusTextBox(int id) {
    g_focusedBox = id;
    g_selectAll = false;
}

bool AnyTextBoxFocused() { return g_focusedBox >= 0; }

void ClearFocus() {
    g_focusedBox = -1;
    g_selectAll = false;
}

void StatTile(Rectangle r, const std::string& title, const std::string& value, Color accent) {
    Angled(r, theme::kPanel, 12.0f);
    Fill(Rectangle{r.x, r.y, 4.0f, r.height - 12.0f}, accent);
    Text(title, r.x + 18.0f, r.y + 12.0f, 18.0f, theme::kTextDim);
    TextBold(value, r.x + 18.0f, r.y + 38.0f, 34.0f, theme::kText);
}

// ---------------------------------------------------------------------------
// Charts

namespace {

void ChartFrame(Rectangle r, float yMin, float yMax, const std::string& yLabel) {
    Fill(r, theme::kPanel);
    for (int i = 0; i <= 4; ++i) {
        const float y = r.y + r.height - r.height * static_cast<float>(i) / 4.0f;
        Line(Vector2{r.x, y}, Vector2{r.x + r.width, y}, 1.0f, Alpha(theme::kLine, 0.6f));
        const float v = yMin + (yMax - yMin) * static_cast<float>(i) / 4.0f;
        Text(FormatValue(std::fabs(yMax - yMin) < 10.0f ? "%.2f" : "%.0f", static_cast<double>(v)), r.x - 8.0f,
             y - 9.0f, 16.0f, theme::kTextDim, Align::Right);
    }
    Text(yLabel, r.x, r.y - 26.0f, 18.0f, theme::kTextDim);
}

void Range(const std::vector<float>& v, float& lo, float& hi) {
    lo = 0.0f;
    hi = 1.0f;
    if (v.empty()) return;
    lo = *std::min_element(v.begin(), v.end());
    hi = *std::max_element(v.begin(), v.end());
    if (hi - lo < 1e-3f) {
        lo -= 1.0f;
        hi += 1.0f;
    }
    const bool nonNegative = lo >= 0.0f;
    const float pad = (hi - lo) * 0.1f;
    lo -= pad;
    hi += pad;
    if (nonNegative && lo < 0.0f) lo = 0.0f;  // never show negative ticks for positive data
}

}  // namespace

void LineChart(Rectangle r, const std::vector<float>& values, Color color, int highlight, const std::string& yLabel) {
    float lo = 0.0f, hi = 1.0f;
    Range(values, lo, hi);
    ChartFrame(r, lo, hi, yLabel);
    if (values.empty()) {
        Text("No runs yet", r.x + r.width * 0.5f, r.y + r.height * 0.5f - 12.0f, 24.0f, theme::kTextDim, Align::Center);
        return;
    }
    const size_t n = values.size();
    auto pt = [&](size_t i) {
        const float x = n == 1 ? r.x + r.width * 0.5f : r.x + 12.0f + (r.width - 24.0f) * static_cast<float>(i) / static_cast<float>(n - 1);
        const float y = r.y + r.height - (values[i] - lo) / (hi - lo) * r.height;
        return Vector2{x, y};
    };
    for (size_t i = 1; i < n; ++i) Line(pt(i - 1), pt(i), 3.0f, color);
    for (size_t i = 0; i < n; ++i) {
        const Vector2 p = pt(i);
        const bool hl = static_cast<int>(i) == highlight;
        const float s = hl ? 7.0f : 4.0f;
        Fill(Rectangle{p.x - s, p.y - s, s * 2.0f, s * 2.0f}, hl ? theme::kWarn : color);
    }
}

void ScatterChart(Rectangle r, const std::vector<ChartPoint>& points, Color color, float markerX,
                  const std::string& xLabel, const std::string& yLabel) {
    std::vector<float> xs, ys;
    for (const ChartPoint& p : points) {
        xs.push_back(p.x);
        ys.push_back(p.y);
    }
    if (!std::isnan(markerX)) xs.push_back(markerX);
    float xlo = 0.0f, xhi = 1.0f, ylo = 0.0f, yhi = 100.0f;
    Range(xs, xlo, xhi);
    Range(ys, ylo, yhi);
    ChartFrame(r, ylo, yhi, yLabel);
    auto map = [&](float x, float y) {
        return Vector2{r.x + (x - xlo) / (xhi - xlo) * r.width, r.y + r.height - (y - ylo) / (yhi - ylo) * r.height};
    };
    // X axis labels.
    for (int i = 0; i <= 4; ++i) {
        const float x = xlo + (xhi - xlo) * static_cast<float>(i) / 4.0f;
        const Vector2 p = map(x, ylo);
        Text(FormatValue("%.3f", static_cast<double>(x)), p.x, r.y + r.height + 6.0f, 16.0f, theme::kTextDim, Align::Center);
    }
    Text(xLabel, r.x + r.width, r.y + r.height + 28.0f, 18.0f, theme::kTextDim, Align::Right);
    if (!std::isnan(markerX)) {
        const Vector2 top = map(markerX, yhi), bottom = map(markerX, ylo);
        Line(top, bottom, 2.0f, theme::kGood);
    }
    for (const ChartPoint& p : points) {
        const Vector2 s = map(p.x, p.y);
        Tri(Vector2{s.x, s.y - 8.0f}, Vector2{s.x - 7.0f, s.y + 6.0f}, Vector2{s.x + 7.0f, s.y + 6.0f}, color);
    }
}

}  // namespace ui
