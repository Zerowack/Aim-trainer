// effects.h - Short-lived visual effects (hit particles). Purely cosmetic:
// nothing here affects hit detection or timing.
#pragma once

#include <vector>

#include "raylib.h"
#include "rng.h"
#include "world.h"

class Effects {
public:
    // Spray of small glowing cubes from 'pos'.
    void Burst(Vector3 pos, Color color, int count, float speed, Rng& rng);
    void Update(double dt);
    void Draw(const World& world) const;
    void Clear() { particles_.clear(); }

private:
    struct Particle {
        Vector3 pos;
        Vector3 vel;
        float life;
        float maxLife;
        float size;
        Color color;
    };
    std::vector<Particle> particles_;
};
