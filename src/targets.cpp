// targets.cpp - hit testing and drawing for spheres and humanoids.
#include "targets.h"

#include <cmath>

#include "camera.h"

namespace {

BoundingBox MakeBox(Vector3 c, Vector3 size) {
    return BoundingBox{Vector3{c.x - size.x * 0.5f, c.y - size.y * 0.5f, c.z - size.z * 0.5f},
                       Vector3{c.x + size.x * 0.5f, c.y + size.y * 0.5f, c.z + size.z * 0.5f}};
}

Vector3 HeadCenter(const Target& t) { return Vector3{t.pos.x, t.pos.y + hitbox::kHeadCenterY, t.pos.z}; }
Vector3 TorsoCenter(const Target& t) { return Vector3{t.pos.x, t.pos.y + hitbox::kTorsoCenterY, t.pos.z}; }
Vector3 LegCenter(const Target& t) { return Vector3{t.pos.x, t.pos.y + hitbox::kLegHeight * 0.5f, t.pos.z}; }

Color Mix(Color a, Color b, float k) {
    auto lerp = [k](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * k);
    };
    return Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 255};
}

}  // namespace

TargetHit RaycastTarget(const Target& t, const Ray& ray) {
    TargetHit result;
    if (t.kind == TargetKind::Sphere) {
        const RayCollision rc = GetRayCollisionSphere(ray, t.pos, t.radius);
        result.hit = rc.hit;
        result.distance = rc.distance;
        return result;
    }
    // Humanoid: head sphere + torso box + legs box. Nearest part wins.
    const RayCollision head = GetRayCollisionSphere(ray, HeadCenter(t), hitbox::kHeadRadius);
    const RayCollision torso = GetRayCollisionBox(
        ray, MakeBox(TorsoCenter(t), Vector3{hitbox::kTorsoWidth, hitbox::kTorsoHeight, hitbox::kTorsoDepth}));
    const RayCollision legs = GetRayCollisionBox(
        ray, MakeBox(LegCenter(t), Vector3{hitbox::kLegWidth, hitbox::kLegHeight, hitbox::kLegDepth}));
    float best = 1e30f;
    if (head.hit && head.distance < best) { best = head.distance; result.hit = true; result.head = true; }
    if (torso.hit && torso.distance < best) { best = torso.distance; result.hit = true; result.head = false; }
    if (legs.hit && legs.distance < best) { best = legs.distance; result.hit = true; result.head = false; }
    result.distance = best;
    return result;
}

Vector3 TargetAimPoint(const Target& t, bool head) {
    if (t.kind == TargetKind::Sphere) return t.pos;
    return head ? HeadCenter(t) : TorsoCenter(t);
}

double TargetAngularRadius(const Target& t, Vector3 eye, bool head) {
    const Vector3 p = TargetAimPoint(t, head);
    const double dx = p.x - eye.x, dy = p.y - eye.y, dz = p.z - eye.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    double r = t.radius;
    if (t.kind == TargetKind::Humanoid) r = head ? hitbox::kHeadRadius : hitbox::kTorsoWidth * 0.5;
    if (dist <= r) return 90.0;
    return val::RadToDeg(std::asin(r / dist));
}

void DrawTarget(const World& world, const Target& t, Color base) {
    Color c = base;
    if (t.beingHit) c = Mix(base, Color{255, 255, 255, 255}, 0.45f);
    if (t.hitFlash > 0.0) c = Mix(c, Color{255, 255, 255, 255}, 0.7f);

    if (t.kind == TargetKind::Sphere) {
        world.DrawSphereLit(t.pos, t.radius, c);
        return;
    }
    const Color body = Shade(c, 0.85f);
    world.DrawBoxLit(LegCenter(t), Vector3{hitbox::kLegWidth, hitbox::kLegHeight, hitbox::kLegDepth}, Shade(c, 0.7f), Surface::Target);
    world.DrawBoxLit(TorsoCenter(t), Vector3{hitbox::kTorsoWidth, hitbox::kTorsoHeight, hitbox::kTorsoDepth}, body, Surface::Target);
    world.DrawSphereLit(HeadCenter(t), hitbox::kHeadRadius, c);
}
