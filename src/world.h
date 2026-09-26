// world.h - The shooting range environment and lit 3D drawing helpers.
#pragma once

#include <vector>

#include "raylib.h"

// What a cover box looks like (it always blocks movement and bullets).
enum class CoverStyle : int {
    Range = 0,  // plain range block with a red rim (training modes)
    Wall,       // concrete wall with panel seams
    Crate,      // crate with a framed edge
    Barrier     // low concrete barrier with a painted stripe
};

struct Box {
    Vector3 center;
    Vector3 size;
    CoverStyle style = CoverStyle::Range;
    Color tint = {0, 0, 0, 0};  // optional colour override (alpha 0 = material default)
};

// How a surface is shaded by the range shader.
enum class Surface {
    Plain,   // matte, fogged
    Grid,    // matte with anti-aliased grid lines (floor / walls), fogged
    Target,  // glossy with a slight glow, barely fogged so it always pops
    Glow,    // fully emissive (hit particles)
    Wall,    // concrete: panel seams, grain, bevelled edges
    Floor,   // floor tiles with slight colour variation
    Crate,   // framed edges
    Metal,   // dark, shiny (weapons)
    Cloth,   // matte, barely fogged (agent suits)
    Shadow   // black with the colour's alpha (blob shadows)
};

class World {
public:
    bool Init();
    void Shutdown();

    // Call once per frame inside BeginMode3D, before any other drawing.
    void BeginFrame(Vector3 eye, float brightness);
    // Override the fog (modes with their own map and sky colour).
    void SetFog(Color color, float density) const;
    void DrawRange() const;
    // Background colour matching the fog (use for ClearBackground).
    Color SkyColor(float brightness) const;

    // Lit primitives (use the range shader, unlike raylib's DrawSphere).
    void DrawSphereLit(Vector3 center, float radius, Color color, Surface s = Surface::Target) const;
    void DrawBoxLit(Vector3 center, Vector3 size, Color color, Surface s = Surface::Plain) const;
    // Box rotated about the vertical axis (yaw in degrees, camera convention:
    // 0 faces -z).
    void DrawBoxYaw(Vector3 center, Vector3 size, float yawDeg, Color color, Surface s = Surface::Plain) const;
    // Box with an arbitrary orientation: 'right', 'up', 'forward' are unit axes.
    void DrawBoxAxes(Vector3 center, Vector3 size, Vector3 right, Vector3 up, Vector3 forward, Color color,
                     Surface s = Surface::Plain) const;
    // Rounded limb from a to b (cylinder + two spheres).
    void DrawCapsuleLit(Vector3 a, Vector3 b, float radius, Color color, Surface s = Surface::Cloth) const;
    // Soft dark disc on the floor.
    void DrawBlobShadow(Vector3 feet, float radius, float strength) const;
    // Floor + outer walls for an arena of the given size (VS Bot / Sniper).
    void DrawArenaFloor(float halfX, float halfZ, Color color) const;

    // Cover boxes that block shots (used by peek practice).
    void SetCovers(const std::vector<Box>& covers) { covers_ = covers; }
    void ClearCovers() { covers_.clear(); }
    const std::vector<Box>& Covers() const { return covers_; }
    void DrawCovers() const;
    // Distance to the nearest cover hit by the ray, or a huge value.
    float RaycastCovers(const Ray& ray) const;

private:
    void UseSurface(Surface s, float gridCell = 1.0f) const;
    void SetBoxFrame(Vector3 center, Vector3 half) const;

    Shader shader_ = {};
    Model sphere_ = {};
    Model cube_ = {};
    Model cylinder_ = {};
    Model disc_ = {};
    int locBoxCenter_ = -1;
    int locBoxHalf_ = -1;
    int locPattern_ = -1;
    int locShadow_ = -1;
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
