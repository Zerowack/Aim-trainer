// crosshair.h - Valorant-style crosshair settings, drawing and share codes.
#pragma once

#include <string>

#include "raylib.h"

struct Crosshair {
    unsigned char r = 0, g = 255, b = 0;  // line colour

    bool outline = true;
    int outlineThickness = 1;   // pixels, 1..6
    float outlineOpacity = 0.5f;

    bool centerDot = false;
    int dotThickness = 2;       // pixels, 1..6
    float dotOpacity = 1.0f;

    bool innerShow = true;
    float innerOpacity = 0.8f;
    int innerLength = 6;        // pixels, 0..20
    int innerThickness = 2;     // pixels, 1..10
    int innerOffset = 3;        // pixels, 0..20

    bool outerShow = false;
    float outerOpacity = 0.35f;
    int outerLength = 2;
    int outerThickness = 2;
    int outerOffset = 10;

    // Valorant's extra line settings. The lines move outwards with the
    // weapon's firing / movement error; with firing error on they also sit
    // 4 px further out at rest, unless 'overrideFiringOffset' is set.
    bool innerSeparateVert = false;  // vertical arms use innerVertLength
    int innerVertLength = 6;
    bool innerFiringError = false;
    bool innerMoveError = false;
    float innerFireMult = 1.0f;
    float innerMoveMult = 1.0f;
    bool outerSeparateVert = false;
    int outerVertLength = 2;
    bool outerFiringError = false;
    bool outerMoveError = false;
    float outerFireMult = 1.0f;
    float outerMoveMult = 1.0f;
    bool overrideFiringOffset = false;
    bool fadeWithFiring = false;  // lines with firing error fade as they spread
};

// Clamps every field into its valid range.
void ClampCrosshair(Crosshair& c);

// Share code of the form
//   XH1;c=00FF00;o=1;ot=1;oa=0.50;d=0;dt=2;da=1.00;i=1;ia=0.80;il=6;it=2;io=3;x=0;xa=0.35;xl=2;xt=2;xo=10
// Keys may appear in any order; missing keys keep their default value.
std::string EncodeCrosshair(const Crosshair& c);
bool DecodeCrosshair(const std::string& code, Crosshair& out, std::string* error);

// Valorant in-game share code (Settings > Crosshair > Import/Export), e.g.
//   0;P;c;5;h;0;f;0;0l;4;0o;2;0a;1;0f;0;1b;0
// Only the primary crosshair ("P" section) is used, including firing /
// movement error, separate vertical lengths and fading. ADS and sniper
// crosshairs are ignored.
bool DecodeValorantCrosshair(const std::string& code, Crosshair& out, std::string* error);

// Accepts either format: tries the Valorant code first when it looks like
// one (starts with "0;"), otherwise the XH1 format.
bool DecodeAnyCrosshair(const std::string& code, Crosshair& out, std::string* error);

// Draws the crosshair centred on pixel (cx, cy). Sizes are screen pixels,
// exactly like Valorant (no resolution scaling). 'scale' enlarges it for the
// editor preview (1 = real size). firePx / movePx: the weapon's current
// firing and movement error in pixels, for lines that show them.
void DrawCrosshair(const Crosshair& c, int cx, int cy, int scale = 1, float firePx = 0.0f, float movePx = 0.0f);
