// ui.h - Minimal immediate-mode UI in a dark, sharp-angled style.
//
// Layout uses "virtual units": the screen is scaled so that it is always at
// least 1600 x 1080 units, regardless of resolution. Widgets take virtual
// coordinates and convert to pixels internally, so the UI looks the same at
// 1080p, 1440p and 4K.
#pragma once

#include <string>
#include <vector>

#include "raylib.h"

namespace ui {

namespace theme {
extern const Color kBg;        // near-black blue
extern const Color kPanel;     // panel fill
extern const Color kPanel2;    // raised elements
extern const Color kLine;      // subtle borders
extern const Color kAccent;    // red accent
extern const Color kAccentDim;
extern const Color kText;      // off-white
extern const Color kTextDim;
extern const Color kGood;
extern const Color kWarn;
}  // namespace theme

enum class Align { Left, Center, Right };

void Init();
void Shutdown();
// Call once per frame before any widget.
void BeginFrame();

float VW();     // virtual width
float VH();     // virtual height
float Scale();  // pixels per virtual unit
Rectangle ToScreen(Rectangle r);
Vector2 ToScreen(Vector2 p);
Vector2 Mouse();  // mouse position in virtual units
bool Hover(Rectangle r);

// Drawing
void Text(const std::string& s, float x, float y, float size, Color c, Align a = Align::Left);
float TextWidth(const std::string& s, float size);
void Fill(Rectangle r, Color c);
void Border(Rectangle r, float thickness, Color c);
// Rectangle with the top-right and bottom-left corners cut at 45 degrees.
void Angled(Rectangle r, Color c, float cut);
void Line(Vector2 a, Vector2 b, float thickness, Color c);
void Tri(Vector2 a, Vector2 b, Vector2 c, Color col);
void Title(const std::string& s, float x, float y, float size);  // title with red slash
// Heavier text (drawn twice with a small offset) for headings and numbers.
void TextBold(const std::string& s, float x, float y, float size, Color c, Align a = Align::Left);
void Circle(Vector2 center, float radius, Color c);
void CircleLines(Vector2 center, float radius, float thickness, Color c);
// The Valtrainer emblem (same design as the app icon), 'size' units square.
void Logo(float x, float y, float size);
// Smoothly animated 0..1 hover amount for a rectangle (for highlights).
float HoverAnim(Rectangle r, bool enabled = true);
void Backdrop();  // full screen background with accent stripes
Color Alpha(Color c, float alpha);

// Widgets. They return true when the value changed / the button was clicked.
bool Button(Rectangle r, const std::string& label, bool primary = false, bool enabled = true);
bool Tab(Rectangle r, const std::string& label, bool active);
bool Toggle(Rectangle r, const std::string& label, bool* value);
bool SliderF(Rectangle r, const std::string& label, float* value, float minV, float maxV, const char* fmt);
bool SliderI(Rectangle r, const std::string& label, int* value, int minV, int maxV);
bool SliderU8(Rectangle r, const std::string& label, unsigned char* value);
// Clickable colour square.
bool Swatch(Rectangle r, Color c, bool selected);
// Word-wrapped paragraph. Returns the height used.
float TextBlock(const std::string& s, float x, float y, float width, float size, Color c);
bool Stepper(Rectangle r, const std::string& label, int* index, const char* const* items, int count);
// Single-line text box. Clicking into it selects everything, so typing or
// Ctrl+V replaces the old value; Ctrl+A selects all, Ctrl+Backspace clears,
// Enter unfocuses.
// Returns true whenever the text changed. 'id' must be unique on screen.
bool TextBox(Rectangle r, int id, std::string* text, size_t maxLen, bool numeric);
bool AnyTextBoxFocused();
// Gives a text box keyboard focus (e.g. "start typing to search").
void FocusTextBox(int id);
void ClearFocus();

void StatTile(Rectangle r, const std::string& title, const std::string& value, Color accent);

// Charts
struct ChartPoint {
    float x;
    float y;
};
// Line chart with optional highlighted point index (-1 for none).
void LineChart(Rectangle r, const std::vector<float>& values, Color color, int highlight,
               const std::string& yLabel);
// Scatter chart with an optional vertical marker line at markerX (NaN = none).
void ScatterChart(Rectangle r, const std::vector<ChartPoint>& points, Color color, float markerX,
                  const std::string& xLabel, const std::string& yLabel);

}  // namespace ui
