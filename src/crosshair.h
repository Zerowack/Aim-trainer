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
};

// Clamps every field into its valid range.
void ClampCrosshair(Crosshair& c);

// Share code of the form
//   XH1;c=00FF00;o=1;ot=1;oa=0.50;d=0;dt=2;da=1.00;i=1;ia=0.80;il=6;it=2;io=3;x=0;xa=0.35;xl=2;xt=2;xo=10
// Keys may appear in any order; missing keys keep their default value.
std::string EncodeCrosshair(const Crosshair& c);
bool DecodeCrosshair(const std::string& code, Crosshair& out, std::string* error);

// Draws the crosshair centred on pixel (cx, cy). 'scale' enlarges it for the
// editor preview (1 = real size).
void DrawCrosshair(const Crosshair& c, int cx, int cy, int scale = 1);
