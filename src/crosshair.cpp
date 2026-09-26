// crosshair.cpp - crosshair drawing and share-code parsing.
#include "crosshair.h"

#include <algorithm>
#include <cctype>
#include <cmath>
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

void AddLines(std::vector<Piece>& out, int cx, int cy, int length, int vertLength, int thickness, int offset,
              unsigned char alpha) {
    if (thickness <= 0 || alpha == 0) return;
    // Like Valorant, the offset is measured from the exact centre of the
    // screen: with offset 0 the four lines meet in the middle. Across the
    // line, the thickness is centred on the centre pixel.
    const int across = -(thickness / 2);  // first pixel across the line
    if (length > 0) {
        out.push_back({cx + offset, cy + across, length, thickness, alpha});           // right
        out.push_back({cx - offset - length, cy + across, length, thickness, alpha});  // left
    }
    if (vertLength > 0) {
        out.push_back({cx + across, cy + offset, thickness, vertLength, alpha});           // down
        out.push_back({cx + across, cy - offset - vertLength, thickness, vertLength, alpha});  // up
    }
}

// Valorant's firing-error lines sit this much further out at rest.
constexpr int kFiringErrorGap = 4;

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
    c.innerVertLength = ClampI(c.innerVertLength, 0, 20);
    c.innerThickness = ClampI(c.innerThickness, 0, 10);
    c.innerOffset = ClampI(c.innerOffset, 0, 20);
    c.outerOpacity = ClampF(c.outerOpacity, 0.0f, 1.0f);
    c.outerLength = ClampI(c.outerLength, 0, 20);
    c.outerVertLength = ClampI(c.outerVertLength, 0, 20);
    c.outerThickness = ClampI(c.outerThickness, 0, 10);
    c.outerOffset = ClampI(c.outerOffset, 0, 40);
    c.innerFireMult = ClampF(c.innerFireMult, 0.0f, 3.0f);
    c.innerMoveMult = ClampF(c.innerMoveMult, 0.0f, 3.0f);
    c.outerFireMult = ClampF(c.outerFireMult, 0.0f, 3.0f);
    c.outerMoveMult = ClampF(c.outerMoveMult, 0.0f, 3.0f);
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
    std::string code = buf;
    // Valorant extras, only written when they differ from the defaults.
    const Crosshair d;
    auto add = [&](const char* key, const std::string& v) { code += std::string(";") + key + "=" + v; };
    auto num = [](double v) { char b[32]; std::snprintf(b, sizeof(b), "%.2f", v); return std::string(b); };
    if (c.innerSeparateVert != d.innerSeparateVert) add("ig", c.innerSeparateVert ? "1" : "0");
    if (c.innerVertLength != d.innerVertLength) add("iv", std::to_string(c.innerVertLength));
    if (c.innerFiringError != d.innerFiringError) add("ife", c.innerFiringError ? "1" : "0");
    if (c.innerMoveError != d.innerMoveError) add("ime", c.innerMoveError ? "1" : "0");
    if (c.innerFireMult != d.innerFireMult) add("ifm", num(c.innerFireMult));
    if (c.innerMoveMult != d.innerMoveMult) add("imm", num(c.innerMoveMult));
    if (c.outerSeparateVert != d.outerSeparateVert) add("xg", c.outerSeparateVert ? "1" : "0");
    if (c.outerVertLength != d.outerVertLength) add("xv", std::to_string(c.outerVertLength));
    if (c.outerFiringError != d.outerFiringError) add("xfe", c.outerFiringError ? "1" : "0");
    if (c.outerMoveError != d.outerMoveError) add("xme", c.outerMoveError ? "1" : "0");
    if (c.outerFireMult != d.outerFireMult) add("xfm", num(c.outerFireMult));
    if (c.outerMoveMult != d.outerMoveMult) add("xmm", num(c.outerMoveMult));
    if (c.overrideFiringOffset != d.overrideFiringOffset) add("ov", c.overrideFiringOffset ? "1" : "0");
    if (c.fadeWithFiring != d.fadeWithFiring) add("fd", c.fadeWithFiring ? "1" : "0");
    return code;
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
        else if (key == "ig") ok = ParseBool(val, c.innerSeparateVert);
        else if (key == "iv") ok = ParseInt(val, c.innerVertLength);
        else if (key == "ife") ok = ParseBool(val, c.innerFiringError);
        else if (key == "ime") ok = ParseBool(val, c.innerMoveError);
        else if (key == "ifm") ok = ParseFloat(val, c.innerFireMult);
        else if (key == "imm") ok = ParseFloat(val, c.innerMoveMult);
        else if (key == "xg") ok = ParseBool(val, c.outerSeparateVert);
        else if (key == "xv") ok = ParseInt(val, c.outerVertLength);
        else if (key == "xfe") ok = ParseBool(val, c.outerFiringError);
        else if (key == "xme") ok = ParseBool(val, c.outerMoveError);
        else if (key == "xfm") ok = ParseFloat(val, c.outerFireMult);
        else if (key == "xmm") ok = ParseFloat(val, c.outerMoveMult);
        else if (key == "ov") ok = ParseBool(val, c.overrideFiringOffset);
        else if (key == "fd") ok = ParseBool(val, c.fadeWithFiring);
        else return fail("Unknown key '" + key + "'.");
        if (!ok) return fail("Bad value '" + val + "' for key '" + key + "'.");
        if (next >= code.size()) break;
    }
    ClampCrosshair(c);
    out = c;
    if (error) error->clear();
    return true;
}

bool DecodeValorantCrosshair(const std::string& codeIn, Crosshair& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    const std::string code = Trim(codeIn);

    // Split on ';' (ignoring stray spaces / line breaks from copy-paste).
    std::vector<std::string> tok;
    size_t pos = 0;
    while (pos <= code.size()) {
        size_t next = code.find(';', pos);
        if (next == std::string::npos) next = code.size();
        tok.push_back(Trim(code.substr(pos, next - pos)));
        pos = next + 1;
    }
    while (!tok.empty() && tok.back().empty()) tok.pop_back();
    if (tok.empty() || tok[0] != "0") return fail("Not a Valorant crosshair code (it should start with \"0;\").");

    // Valorant's defaults: every code only lists values that differ from these.
    Crosshair c;
    c.r = 255; c.g = 255; c.b = 255;
    c.outline = true; c.outlineThickness = 1; c.outlineOpacity = 0.5f;
    c.centerDot = false; c.dotThickness = 2; c.dotOpacity = 1.0f;
    c.innerShow = true; c.innerOpacity = 0.8f; c.innerLength = 6; c.innerThickness = 2; c.innerOffset = 3;
    c.outerShow = true; c.outerOpacity = 0.35f; c.outerLength = 2; c.outerThickness = 2; c.outerOffset = 10;
    c.innerSeparateVert = false; c.innerVertLength = 6; c.innerFiringError = true; c.innerMoveError = false;
    c.outerSeparateVert = false; c.outerVertLength = 2; c.outerFiringError = true; c.outerMoveError = true;
    c.innerFireMult = c.innerMoveMult = c.outerFireMult = c.outerMoveMult = 1.0f;
    c.overrideFiringOffset = false; c.fadeWithFiring = true;
    bool innerVertSet = false, outerVertSet = false;

    // Valorant's colour presets (c;0..7); c;8 = custom colour from "u".
    static const unsigned char kPresets[8][3] = {{255, 255, 255}, {0, 255, 0},   {127, 255, 0}, {223, 255, 0},
                                                 {255, 255, 0},   {0, 255, 255}, {255, 0, 255}, {255, 0, 0}};
    int colorIndex = 0;
    std::string customHex;

    std::string section;  // "" = general, "P" primary, "A" ADS, "S" sniper
    bool sawPrimary = false;
    size_t i = 1;
    while (i < tok.size()) {
        const std::string& t = tok[i];
        if (t == "P" || t == "A" || t == "S") {
            section = t;
            if (t == "P") sawPrimary = true;
            ++i;
            continue;
        }
        if (i + 1 >= tok.size()) break;  // key without value: ignore the tail
        const std::string key = t;
        const std::string val = Lower(tok[i + 1]);
        i += 2;
        if (section != "P") continue;

        int iv = 0;
        float fv = 0.0f;
        bool bv = false;
        bool ok = true;
        if (key == "c") { ok = ParseInt(val, colorIndex); }
        else if (key == "u") { customHex = val; }
        else if (key == "h") { ok = ParseBool(val, bv); c.outline = bv; }
        else if (key == "t") { ok = ParseInt(val, iv); c.outlineThickness = iv; }
        else if (key == "o") { ok = ParseFloat(val, fv); c.outlineOpacity = fv; }
        else if (key == "d") { ok = ParseBool(val, bv); c.centerDot = bv; }
        else if (key == "z") { ok = ParseInt(val, iv); c.dotThickness = iv; }
        else if (key == "a") { ok = ParseFloat(val, fv); c.dotOpacity = fv; }
        else if (key == "0b") { ok = ParseBool(val, bv); c.innerShow = bv; }
        else if (key == "0t") { ok = ParseInt(val, iv); c.innerThickness = iv; }
        else if (key == "0l") { ok = ParseInt(val, iv); c.innerLength = iv; }
        else if (key == "0o") { ok = ParseInt(val, iv); c.innerOffset = iv; }
        else if (key == "0a") { ok = ParseFloat(val, fv); c.innerOpacity = fv; }
        else if (key == "1b") { ok = ParseBool(val, bv); c.outerShow = bv; }
        else if (key == "1t") { ok = ParseInt(val, iv); c.outerThickness = iv; }
        else if (key == "1l") { ok = ParseInt(val, iv); c.outerLength = iv; }
        else if (key == "1o") { ok = ParseInt(val, iv); c.outerOffset = iv; }
        else if (key == "1a") { ok = ParseFloat(val, fv); c.outerOpacity = fv; }
        else if (key == "0g") { ok = ParseBool(val, bv); c.innerSeparateVert = bv; }
        else if (key == "0v") { ok = ParseInt(val, iv); c.innerVertLength = iv; innerVertSet = true; }
        else if (key == "0f") { ok = ParseBool(val, bv); c.innerFiringError = bv; }
        else if (key == "0m") { ok = ParseBool(val, bv); c.innerMoveError = bv; }
        else if (key == "0e") { ok = ParseFloat(val, fv); c.innerFireMult = fv; }
        else if (key == "0s") { ok = ParseFloat(val, fv); c.innerMoveMult = fv; }
        else if (key == "1g") { ok = ParseBool(val, bv); c.outerSeparateVert = bv; }
        else if (key == "1v") { ok = ParseInt(val, iv); c.outerVertLength = iv; outerVertSet = true; }
        else if (key == "1f") { ok = ParseBool(val, bv); c.outerFiringError = bv; }
        else if (key == "1m") { ok = ParseBool(val, bv); c.outerMoveError = bv; }
        else if (key == "1e") { ok = ParseFloat(val, fv); c.outerFireMult = fv; }
        else if (key == "1s") { ok = ParseFloat(val, fv); c.outerMoveMult = fv; }
        else if (key == "m") { ok = ParseBool(val, bv); c.overrideFiringOffset = bv; }
        else if (key == "f") { ok = ParseBool(val, bv); c.fadeWithFiring = bv; }
        // Anything else (e.g. "s" advanced options) doesn't change the look.
        if (!ok) return fail("Bad value '" + val + "' for '" + key + "'.");
    }
    if (!sawPrimary && tok.size() > 1) return fail("No primary crosshair (\"P\") section in the code.");
    // Without its own value the vertical length follows the horizontal one.
    if (!innerVertSet) c.innerVertLength = c.innerLength;
    if (!outerVertSet) c.outerVertLength = c.outerLength;

    if (colorIndex == 8 && customHex.size() >= 6) {
        char* end = nullptr;
        const std::string rgb = customHex.substr(0, 6);
        const unsigned long v = std::strtoul(rgb.c_str(), &end, 16);
        if (!end || *end != '\0') return fail("Bad custom colour '" + customHex + "'.");
        c.r = static_cast<unsigned char>((v >> 16) & 0xFF);
        c.g = static_cast<unsigned char>((v >> 8) & 0xFF);
        c.b = static_cast<unsigned char>(v & 0xFF);
    } else if (colorIndex >= 0 && colorIndex < 8) {
        c.r = kPresets[colorIndex][0];
        c.g = kPresets[colorIndex][1];
        c.b = kPresets[colorIndex][2];
    }

    ClampCrosshair(c);
    out = c;
    if (error) error->clear();
    return true;
}

bool DecodeAnyCrosshair(const std::string& code, Crosshair& out, std::string* error) {
    const std::string t = Trim(code);
    if (t.rfind("0;", 0) == 0 || t == "0") return DecodeValorantCrosshair(t, out, error);
    return DecodeCrosshair(t, out, error);
}

void DrawCrosshair(const Crosshair& cIn, int cx, int cy, int scale, float firePx, float movePx) {
    Crosshair c = cIn;
    ClampCrosshair(c);
    if (scale < 1) scale = 1;
    firePx = std::max(0.0f, firePx);
    movePx = std::max(0.0f, movePx);

    // Gap of one line set: its offset, plus Valorant's error behaviour.
    auto gapFor = [&](int offset, bool fireErr, float fireMult, bool moveErr, float moveMult) {
        float g = static_cast<float>(offset);
        if (fireErr) g += (c.overrideFiringOffset ? 0.0f : static_cast<float>(kFiringErrorGap)) + firePx * fireMult;
        if (moveErr) g += movePx * moveMult;
        return static_cast<int>(std::lround(g * static_cast<float>(scale)));
    };
    auto alphaFor = [&](float opacity, bool fireErr) {
        float a = opacity;
        if (c.fadeWithFiring && fireErr) a *= std::max(0.0f, 1.0f - firePx / 12.0f);
        return AlphaFrom(a);
    };

    std::vector<Piece> pieces;
    if (c.innerShow) {
        AddLines(pieces, cx, cy, c.innerLength * scale, (c.innerSeparateVert ? c.innerVertLength : c.innerLength) * scale,
                 c.innerThickness * scale,
                 gapFor(c.innerOffset, c.innerFiringError, c.innerFireMult, c.innerMoveError, c.innerMoveMult),
                 alphaFor(c.innerOpacity, c.innerFiringError));
    }
    if (c.outerShow) {
        AddLines(pieces, cx, cy, c.outerLength * scale, (c.outerSeparateVert ? c.outerVertLength : c.outerLength) * scale,
                 c.outerThickness * scale,
                 gapFor(c.outerOffset, c.outerFiringError, c.outerFireMult, c.outerMoveError, c.outerMoveMult),
                 alphaFor(c.outerOpacity, c.outerFiringError));
    }
    if (c.centerDot) {
        const int t = c.dotThickness * scale;
        const int half = t / 2;
        pieces.push_back({cx - half, cy - half, t, t, AlphaFrom(c.dotOpacity)});
    }
    if (pieces.empty()) return;

    // Rasterise into a small pixel buffer so overlapping pieces are merged:
    // each pixel gets the strongest opacity of the pieces covering it, and
    // the outline is drawn once around the combined shape (like Valorant),
    // instead of darker spots where pieces overlap.
    const int o = c.outline ? c.outlineThickness * scale : 0;
    int x0 = pieces[0].x, y0 = pieces[0].y, x1 = x0, y1 = y0;
    for (const Piece& p : pieces) {
        x0 = std::min(x0, p.x);
        y0 = std::min(y0, p.y);
        x1 = std::max(x1, p.x + p.w);
        y1 = std::max(y1, p.y + p.h);
    }
    x0 -= o;
    y0 -= o;
    x1 += o;
    y1 += o;
    const int W = x1 - x0, H = y1 - y0;
    if (W <= 0 || H <= 0) return;
    const auto idx = [W](int x, int y) { return static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x); };

    std::vector<unsigned char> fill(static_cast<size_t>(W) * static_cast<size_t>(H), 0);
    for (const Piece& p : pieces) {
        for (int y = p.y; y < p.y + p.h; ++y) {
            for (int x = p.x; x < p.x + p.w; ++x) {
                unsigned char& a = fill[idx(x - x0, y - y0)];
                a = std::max(a, p.alpha);
            }
        }
    }

    // Draws horizontal runs of pixels where 'value(x, y)' is non-zero and equal.
    auto drawRuns = [&](const std::vector<unsigned char>& buf, Color col) {
        for (int y = 0; y < H; ++y) {
            int x = 0;
            while (x < W) {
                const unsigned char a = buf[idx(x, y)];
                if (a == 0) {
                    ++x;
                    continue;
                }
                int end = x + 1;
                while (end < W && buf[idx(end, y)] == a) ++end;
                DrawRectangle(x0 + x, y0 + y, end - x, 1, Color{col.r, col.g, col.b, a});
                x = end;
            }
        }
    };

    if (o > 0) {
        // Outline = shape grown by 'o' pixels (square dilation, done as a
        // horizontal pass then a vertical pass).
        std::vector<unsigned char> grownX(fill.size(), 0), outline(fill.size(), 0);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                if (fill[idx(x, y)] == 0) continue;
                for (int k = std::max(0, x - o); k <= std::min(W - 1, x + o); ++k) grownX[idx(k, y)] = 1;
            }
        }
        const unsigned char oa = AlphaFrom(c.outlineOpacity);
        for (int x = 0; x < W; ++x) {
            for (int y = 0; y < H; ++y) {
                if (grownX[idx(x, y)] == 0) continue;
                for (int k = std::max(0, y - o); k <= std::min(H - 1, y + o); ++k) outline[idx(x, k)] = oa;
            }
        }
        drawRuns(outline, Color{0, 0, 0, 255});
    }
    drawRuns(fill, Color{c.r, c.g, c.b, 255});
}
