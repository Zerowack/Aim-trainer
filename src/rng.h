// rng.h - Small random number helper.
#pragma once

#include <cstdint>
#include <random>

class Rng {
public:
    explicit Rng(uint64_t seed = std::random_device{}()) : engine_(seed) {}

    double Uniform(double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(engine_); }
    float UniformF(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(engine_); }
    // Inclusive range.
    int Int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(engine_); }
    bool Chance(double p) { return Uniform(0.0, 1.0) < p; }
    // -1 or +1
    int Sign() { return Chance(0.5) ? 1 : -1; }

private:
    std::mt19937_64 engine_;
};
