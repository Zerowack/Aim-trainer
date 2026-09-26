// world.h - The shooting range environment and lit 3D drawing helpers.
#pragma once

#include <vector>

#include "raylib.h"

struct Box {
    Vector3 center;
    Vector3 size;
};

// How a surface is shaded by the range shader.
enum class Surface {
    Plain,   // matte, fogged
    Grid,    // matte with anti-aliased grid lines (floor / walls), fogged
    Target,  // glossy with a slight glow, barely fogged so it always pops
    Glow     // fully emissive (hit particles)
};

class World {
public:
    bool Init();
    void Shutdown();

    // Call once per frame inside BeginMode3D, before any other drawing.
    void BeginFrame(Vector3 eye, float brightness);
    void DrawRange() const;
    // Background colour matching the fog (use for ClearBackground).
    Color SkyColor(float brightness) const;

    // Lit primitives (use the range shader, unlike raylib's DrawSphere).
    void DrawSphereLit(Vector3 center, float radius, Color color, Surface s = Surface::Target) const;
    void DrawBoxLit(Vector3 center, Vector3 size, Color color, Surface s = Surface::Plain) const;

    // Cover boxes that block shots (used by peek practice).
    void SetCovers(const std::vector<Box>& covers) { covers_ = covers; }
    void ClearCovers() { covers_.clear(); }
    const std::vector<Box>& Covers() const { return covers_; }
    void DrawCovers() const;
    // Distance to the nearest cover hit by the ray, or a huge value.
    float RaycastCovers(const Ray& ray) const;

private:
    void UseSurface(Surface s, float gridCell = 1.0f) const;

    Shader shader_ = {};
    Model sphere_ = {};
    Model cube_ = {};
    int locViewPos_ = -1;
    int locFogColor_ = -1;
    int locFogDensity_ = -1;
    int locGrid_ = -1;
    int locGridCell_ = -1;
    int locGridColor_ = -1;
    int locSpecular_ = -1;
    int locEmissive_ = -1;
    int locFogAmount_ = -1;
    bool ready_ = false;
    float brightness_ = 1.0f;
    mutable int currentSurface_ = -1;
    mutable float currentCell_ = -1.0f;
    std::vector<Box> covers_;
};

// Multiplies the RGB channels of a colour by 'k' (clamped).
Color Shade(Color c, float k);
