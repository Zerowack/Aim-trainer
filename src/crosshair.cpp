// crosshair.cpp - crosshair drawing and share-code parsing.
#include "crosshair.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

int ClampI(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
float ClampF(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

unsigned char AlphaFrom(float opacity) {
    return static_cast<unsigned char>(ClampF(opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// Rectangles are collected first so the outline of every piece can be drawn
// underneath all fills (like Valorant does).
struct Piece {
    int x, y, w, h;
    unsigned char alpha;
};

void AddLines(std::vector<Piece>& out, int cx, int cy, int length, int thickness, int offset,
              unsigned char alpha) {
    if (length <= 0 || thickness <= 0) return;
    // The centre "block" (where the lines would meet) spans
    // [c - half, c - half + thickness) on each axis, so odd and even
    // thicknesses both stay symmetric around the centre pixel.
    const int half = thickness / 2;
    const int lo = -half;              // first pixel of the centre block
    const int hi = -half + thickness;  // one past the last pixel
    out.push_back({cx + hi + offset, cy + lo, length, thickness, alpha});           // right
    out.push_back({cx + lo - offset - length, cy + lo, length, thickness, alpha});  // left
    out.push_back({cx + lo, cy + hi + offset, thickness, length, alpha});           // down
    out.push_back({cx + lo, cy + lo - offset - length, thickness, length, alpha});  // up
}

std::string Lower(std::string s) {
    for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return s;
}

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

bool ParseInt(const std::string& v, int& out) {
    char* end = nullptr;
    const long x = std::strtol(v.c_str(), &end, 10);
    if (end == v.c_str() || *end != '\0') return false;
    out = static_cast<int>(x);
    return true;
}

bool ParseFloat(const std::string& v, float& out) {
    char* end = nullptr;
    const double x = std::strtod(v.c_str(), &end);
    if (end == v.c_str() || *end != '\0') return false;
    out = static_cast<float>(x);
    return true;
}

bool ParseBool(const std::string& v, bool& out) {
    if (v == "1" || v == "true" || v == "on") { out = true; return true; }
    if (v == "0" || v == "false" || v == "off") { out = false; return true; }
    return false;
}

}  // namespace

void ClampCrosshair(Crosshair& c) {
    c.outlineThickness = ClampI(c.outlineThickness, 1, 6);
    c.outlineOpacity = ClampF(c.outlineOpacity, 0.0f, 1.0f);
    c.dotThickness = ClampI(c.dotThickness, 1, 6);
    c.dotOpacity = ClampF(c.dotOpacity, 0.0f, 1.0f);
    c.innerOpacity = ClampF(c.innerOpacity, 0.0f, 1.0f);
    c.innerLength = ClampI(c.innerLength, 0, 20);
    c.innerThickness = ClampI(c.innerThickness, 1, 10);
    c.innerOffset = ClampI(c.innerOffset, 0, 20);
    c.outerOpacity = ClampF(c.outerOpacity, 0.0f, 1.0f);
    c.outerLength = ClampI(c.outerLength, 0, 20);
    c.outerThickness = ClampI(c.outerThickness, 1, 10);
    c.outerOffset = ClampI(c.outerOffset, 0, 40);
}

std::string EncodeCrosshair(const Crosshair& c) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "XH1;c=%02X%02X%02X;o=%d;ot=%d;oa=%.2f;d=%d;dt=%d;da=%.2f;i=%d;ia=%.2f;il=%d;it=%d;io=%d;"
                  "x=%d;xa=%.2f;xl=%d;xt=%d;xo=%d",
                  c.r, c.g, c.b, c.outline ? 1 : 0, c.outlineThickness, static_cast<double>(c.outlineOpacity),
                  c.centerDot ? 1 : 0, c.dotThickness, static_cast<double>(c.dotOpacity), c.innerShow ? 1 : 0,
                  static_cast<double>(c.innerOpacity), c.innerLength, c.innerThickness, c.innerOffset,
                  c.outerShow ? 1 : 0, static_cast<double>(c.outerOpacity), c.outerLength, c.outerThickness,
                  c.outerOffset);
    return buf;
}

bool DecodeCrosshair(const std::string& codeIn, Crosshair& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    const std::string code = Trim(codeIn);
    if (code.empty()) return fail("The code is empty.");

    Crosshair c;  // start from defaults so a code always gives the same result
    size_t pos = 0;
    bool first = true;
    while (pos <= code.size()) {
        size_t next = code.find(';', pos);
        if (next == std::string::npos) next = code.size();
        const std::string token = Trim(code.substr(pos, next - pos));
        pos = next + 1;
        if (token.empty()) {
            if (next >= code.size()) break;
            continue;
        }
        if (first) {
            first = false;
            if (Lower(token) == "xh1") continue;  // optional version tag
        }
        const size_t eq = token.find('=');
        if (eq == std::string::npos) return fail("Expected key=value but found '" + token + "'.");
        const std::string key = Lower(Trim(token.substr(0, eq)));
        const std::string val = Lower(Trim(token.substr(eq + 1)));
        bool ok = true;
        if (key == "c") {
            std::string hex = val;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            char* end = nullptr;
            const unsigned long rgb = std::strtoul(hex.c_str(), &end, 16);
            ok = hex.size() == 6 && end && *end == '\0';
            if (ok) {
                c.r = static_cast<unsigned char>((rgb >> 16) & 0xFF);
                c.g = static_cast<unsigned char>((rgb >> 8) & 0xFF);
                c.b = static_cast<unsigned char>(rgb & 0xFF);
            }
        } else if (key == "o") ok = ParseBool(val, c.outline);
        else if (key == "ot") ok = ParseInt(val, c.outlineThickness);
        else if (key == "oa") ok = ParseFloat(val, c.outlineOpacity);
        else if (key == "d") ok = ParseBool(val, c.centerDot);
        else if (key == "dt") ok = ParseInt(val, c.dotThickness);
        else if (key == "da") ok = ParseFloat(val, c.dotOpacity);
        else if (key == "i") ok = ParseBool(val, c.innerShow);
        else if (key == "ia") ok = ParseFloat(val, c.innerOpacity);
        else if (key == "il") ok = ParseInt(val, c.innerLength);
        else if (key == "it") ok = ParseInt(val, c.innerThickness);
        else if (key == "io") ok = ParseInt(val, c.innerOffset);
        else if (key == "x") ok = ParseBool(val, c.outerShow);
        else if (key == "xa") ok = ParseFloat(val, c.outerOpacity);
        else if (key == "xl") ok = ParseInt(val, c.outerLength);
        else if (key == "xt") ok = ParseInt(val, c.outerThickness);
        else if (key == "xo") ok = ParseInt(val, c.outerOffset);
        else return fail("Unknown key '" + key + "'.");
        if (!ok) return fail("Bad value '" + val + "' for key '" + key + "'.");
        if (next >= code.size()) break;
    }
    ClampCrosshair(c);
    out = c;
    if (error) error->clear();
    return true;
}

void DrawCrosshair(const Crosshair& cIn, int cx, int cy, int scale) {
    Crosshair c = cIn;
    ClampCrosshair(c);
    if (scale < 1) scale = 1;

    std::vector<Piece> pieces;
    if (c.innerShow) {
        AddLines(pieces, cx, cy, c.innerLength * scale, c.innerThickness * scale, c.innerOffset * scale,
                 AlphaFrom(c.innerOpacity));
    }
    if (c.outerShow) {
        AddLines(pieces, cx, cy, c.outerLength * scale, c.outerThickness * scale, c.outerOffset * scale,
                 AlphaFrom(c.outerOpacity));
    }
    if (c.centerDot) {
        const int t = c.dotThickness * scale;
        const int half = t / 2;
        pieces.push_back({cx - half, cy - half, t, t, AlphaFrom(c.dotOpacity)});
    }

    if (c.outline) {
        const int o = c.outlineThickness * scale;
        const unsigned char oa = AlphaFrom(c.outlineOpacity);
        for (const Piece& p : pieces) {
            DrawRectangle(p.x - o, p.y - o, p.w + 2 * o, p.h + 2 * o, Color{0, 0, 0, oa});
        }
    }
    for (const Piece& p : pieces) {
        DrawRectangle(p.x, p.y, p.w, p.h, Color{c.r, c.g, c.b, p.alpha});
    }
}
