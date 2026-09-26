// movement.h - Valorant-style character movement shared by the modes where
// you walk around (VS Bot, Sniper): run / walk / crouch / jump, fast
// acceleration, counter-strafe stops, collision with cover boxes.
//
// All numbers are approximations of how Valorant feels; they are grouped
// here so they are easy to tune.
#pragma once

#include <vector>

#include "raylib.h"
#include "rng.h"
#include "targets.h"
#include "world.h"

namespace mv {

// ---- Movement (metres, seconds) ---------------------------------------------
constexpr float kRunSpeed = 5.40f;     // rifle running speed
constexpr float kWalkSpeed = 2.90f;    // Shift-walk (silent)
constexpr float kCrouchSpeed = 1.90f;  // crouch-walk
constexpr float kAccel = 55.0f;        // with input: full speed in ~0.1 s, counter-strafe stop in ~0.1 s
constexpr float kFriction = 28.0f;     // keys released: stop from a run in ~0.2 s
constexpr float kAirAccel = 8.0f;
constexpr float kJumpSpeed = 5.2f;
constexpr float kGravity = 15.0f;
constexpr float kStandEye = 1.60f;
constexpr float kCrouchEye = 1.12f;
constexpr float kCrouchRate = 1.0f / 0.12f;  // full crouch in 0.12 s
constexpr float kRadius = 0.35f;
constexpr float kAccurateSpeed = 1.35f;  // at or below this the first shot is accurate (~25% of run speed)
constexpr float kFootstepSpeed = 3.2f;   // faster than walking = audible footsteps

// Something that walks around: feet position, velocity, crouch amount.
struct Mover {
    Vector3 pos = {0.0f, 0.0f, 0.0f};
    Vector3 vel = {0.0f, 0.0f, 0.0f};
    float crouch = 0.0f;
    bool onGround = true;

    float Speed() const;
    float EyeHeight() const { return kStandEye + (kCrouchEye - kStandEye) * crouch; }
    Vector3 Eye() const { return Vector3{pos.x, pos.y + EyeHeight(), pos.z}; }
    // Standing still (or slow enough) on the ground: shots are accurate.
    bool Accurate() const { return onGround && Speed() <= kAccurateSpeed; }
};

// Valorant-style ground movement: quick acceleration towards the wished
// velocity, friction when no key is held. Pressing the opposite key uses the
// (stronger) acceleration, which is why counter-strafing stops you faster.
void Accelerate(Mover& m, float wishX, float wishZ, float wishSpeed, float dt);
// Circle-vs-box collision on the ground plane (boxes block everything).
void Collide(Mover& m, const std::vector<Box>& boxes);
// Gravity / landing.
void Vertical(Mover& m, float dt);
// Moves the crouch amount towards 0 or 1 at Valorant's crouch speed.
void Crouch(Mover& m, bool down, float dt);

// One full movement step for scripted movers (bots): crouch, accelerate,
// integrate, gravity, collision. A crouched mover never exceeds crouch speed.
void Step(Mover& m, float wishX, float wishZ, float wishSpeed, bool crouch, float dt, const std::vector<Box>& boxes);

// The local player: reads WASD / Shift (walk) / Ctrl (crouch) / Space (jump)
// relative to the camera yaw and steps the mover. 'speedMult' scales the
// maximum speed (heavy weapons, scoped movement). With live == false only
// the crouch and stopping are simulated (freeze time).
void StepPlayer(Mover& m, double yawDeg, bool live, float speedMult, float dt, const std::vector<Box>& boxes);

// Pushes the mover against the direction it is moving (a counter-strafe).
void CounterStrafe(const Mover& m, float& wishX, float& wishZ, float& wishSpeed);

// The hittable body of a mover.
Target BodyOf(const Mover& m);
// Line of sight from 'from' to the head or chest of 'body' (cover blocks it).
bool CanSee(const World& world, Vector3 from, const Target& body);

Vector3 Sub(Vector3 a, Vector3 b);
float Len(Vector3 v);
double RandNormal(Rng& rng);  // standard normal (Box-Muller)

}  // namespace mv
