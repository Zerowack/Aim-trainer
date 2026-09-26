// world.h - The shooting range environment and lit 3D drawing helpers.
#pragma once

#include <vector>

#include "raylib.h"

struct Box {
    Vector3 center;
    Vector3 size;
};

class World {
public:
    bool Init();
    void Shutdown();

    // Call once per frame inside BeginMode3D, before any other drawing.
    void SetViewPosition(Vector3 eye);
    void DrawRange(float brightness) const;

    // Lit primitives (use the lighting shader, unlike raylib's DrawSphere).
    void DrawSphereLit(Vector3 center, float radius, Color color) const;
    void DrawBoxLit(Vector3 center, Vector3 size, Color color) const;

    // Cover boxes that block shots (used by peek practice).
    void SetCovers(const std::vector<Box>& covers) { covers_ = covers; }
    void ClearCovers() { covers_.clear(); }
    const std::vector<Box>& Covers() const { return covers_; }
    void DrawCovers(float brightness) const;
    // Distance to the nearest cover hit by the ray, or a huge value.
    float RaycastCovers(const Ray& ray) const;

private:
    Shader shader_ = {};
    Model sphere_ = {};
    Model cube_ = {};
    int viewPosLoc_ = -1;
    bool ready_ = false;
    std::vector<Box> covers_;
};

// Multiplies the RGB channels of a colour by 'k' (clamped).
Color Shade(Color c, float k);
