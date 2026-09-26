// effects.cpp - hit particles.
#include "effects.h"

#include <cmath>

void Effects::Burst(Vector3 pos, Color color, int count, float speed, Rng& rng) {
    for (int i = 0; i < count; ++i) {
        // Random direction on a sphere.
        const float z = rng.UniformF(-1.0f, 1.0f);
        const float a = rng.UniformF(0.0f, 6.2831853f);
        const float r = std::sqrt(1.0f - z * z);
        const float s = speed * rng.UniformF(0.4f, 1.0f);
        Particle p;
        p.pos = pos;
        p.vel = Vector3{r * std::cos(a) * s, z * s + 0.8f, r * std::sin(a) * s};
        p.maxLife = rng.UniformF(0.18f, 0.34f);
        p.life = p.maxLife;
        p.size = rng.UniformF(0.035f, 0.07f);
        p.color = color;
        particles_.push_back(p);
    }
    // Keep the list bounded no matter how fast someone clicks.
    if (particles_.size() > 600) particles_.erase(particles_.begin(), particles_.begin() + 200);
}

void Effects::Update(double dtIn) {
    const float dt = static_cast<float>(dtIn);
    for (Particle& p : particles_) {
        p.life -= dt;
        p.vel.y -= 9.0f * dt;  // gravity
        const float drag = 1.0f - 3.0f * dt;
        p.vel.x *= drag;
        p.vel.y *= drag;
        p.vel.z *= drag;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.pos.z += p.vel.z * dt;
    }
    size_t w = 0;
    for (size_t i = 0; i < particles_.size(); ++i) {
        if (particles_[i].life > 0.0f) particles_[w++] = particles_[i];
    }
    particles_.resize(w);
}

void Effects::Draw(const World& world) const {
    for (const Particle& p : particles_) {
        const float k = p.life / p.maxLife;  // shrink as it fades
        const float s = p.size * (0.3f + 0.7f * k);
        world.DrawBoxLit(p.pos, Vector3{s, s, s}, p.color, Surface::Glow);
    }
}
