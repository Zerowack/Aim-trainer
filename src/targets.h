// targets.h - Target shapes, hit testing and drawing.
//
// Units are metres. Sizes approximate Valorant hitboxes: an agent is about
// 1.75 m tall, the torso about 0.5 m wide and the head roughly 0.25 m across.
#pragma once

#include "raylib.h"
#include "world.h"

namespace hitbox {
constexpr float kHeadRadius = 0.125f;   // ~25 cm head
constexpr float kBodySphere = 0.28f;    // "body-sized" sphere used for flicks
constexpr float kLegWidth = 0.36f, kLegHeight = 0.85f, kLegDepth = 0.26f;
constexpr float kTorsoWidth = 0.52f, kTorsoHeight = 0.62f, kTorsoDepth = 0.30f;
constexpr float kNeckGap = 0.02f;
constexpr float kHeadCenterY = kLegHeight + kTorsoHeight + kNeckGap + kHeadRadius;  // ~1.62 m
constexpr float kTorsoCenterY = kLegHeight + kTorsoHeight * 0.5f;
}  // namespace hitbox

enum class TargetKind { Sphere, Humanoid };

struct Target {
    TargetKind kind = TargetKind::Sphere;
    Vector3 pos = {0.0f, 0.0f, 0.0f};  // sphere centre, or the feet of a humanoid
    float radius = hitbox::kBodySphere;
    double spawnTime = 0.0;   // set to the time of the first frame that showed it
    bool presented = false;   // has it been on screen yet?
    double lifetime = 0.0;    // seconds after spawnTime before it expires (0 = never)
    double hitFlash = 0.0;    // visual feedback timer
    bool beingHit = false;    // tracking: crosshair on target while firing
    int tag = 0;              // mode specific (e.g. grid cell)
    float crouch = 0.0f;      // humanoids: 0 = standing, 1 = fully crouched
};

struct TargetHit {
    bool hit = false;
    bool head = false;
    bool legs = false;
    float distance = 0.0f;
};

TargetHit RaycastTarget(const Target& t, const Ray& ray);

// The point a player should aim at (sphere centre / head or chest).
Vector3 TargetAimPoint(const Target& t, bool head);

// Approximate angular radius of the aim zone as seen from 'eye' (degrees).
double TargetAngularRadius(const Target& t, Vector3 eye, bool head);

void DrawTarget(const World& world, const Target& t, Color base);
