// targets.cpp - hit testing and drawing for spheres and humanoids.
#include "targets.h"

#include <cmath>

#include "camera.h"

namespace {

BoundingBox MakeBox(Vector3 c, Vector3 size) {
    return BoundingBox{Vector3{c.x - size.x * 0.5f, c.y - size.y * 0.5f, c.z - size.z * 0.5f},
                       Vector3{c.x + size.x * 0.5f, c.y + size.y * 0.5f, c.z + size.z * 0.5f}};
}

// Crouching shortens the legs (the torso and head drop with them).
float LegHeight(const Target& t) { return hitbox::kLegHeight * (1.0f - 0.5f * t.crouch); }
Vector3 HeadCenter(const Target& t) {
    return Vector3{t.pos.x, t.pos.y + LegHeight(t) + hitbox::kTorsoHeight + hitbox::kNeckGap + hitbox::kHeadRadius, t.pos.z};
}
Vector3 TorsoCenter(const Target& t) {
    return Vector3{t.pos.x, t.pos.y + LegHeight(t) + hitbox::kTorsoHeight * 0.5f, t.pos.z};
}
Vector3 LegCenter(const Target& t) { return Vector3{t.pos.x, t.pos.y + LegHeight(t) * 0.5f, t.pos.z}; }

Color Mix(Color a, Color b, float k) {
    auto lerp = [k](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(static_cast<float>(x) + (static_cast<float>(y) - static_cast<float>(x)) * k);
    };
    return Color{lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b), 255};
}

}  // namespace

// Turns a world-space vector into the humanoid's local frame (its facing
// becomes -z, like the camera at yaw 0), so the boxes can stay axis-aligned.
Vector3 ToLocal(const Target& t, Vector3 v) {
    const double y = val::DegToRad(t.yaw);
    const float c = static_cast<float>(std::cos(y)), s = static_cast<float>(std::sin(y));
    return Vector3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
}

TargetHit RaycastTarget(const Target& t, const Ray& worldRay) {
    TargetHit result;
    if (t.kind == TargetKind::Sphere) {
        const RayCollision rc = GetRayCollisionSphere(worldRay, t.pos, t.radius);
        result.hit = rc.hit;
        result.distance = rc.distance;
        return result;
    }
    // Work in the target's frame, centred on its feet.
    const Vector3 rel = {worldRay.position.x - t.pos.x, worldRay.position.y, worldRay.position.z - t.pos.z};
    Ray ray;
    ray.position = ToLocal(t, rel);
    ray.position.x += t.pos.x;
    ray.position.z += t.pos.z;
    ray.direction = ToLocal(t, worldRay.direction);
    // Humanoid: head sphere + torso box + legs box. Nearest part wins.
    const RayCollision head = GetRayCollisionSphere(ray, HeadCenter(t), hitbox::kHeadRadius);
    const RayCollision torso = GetRayCollisionBox(
        ray, MakeBox(TorsoCenter(t), Vector3{hitbox::kTorsoWidth, hitbox::kTorsoHeight, hitbox::kTorsoDepth}));
    // Crouched knees stick out forward (local -z), so the leg box grows that way.
    Vector3 legC = LegCenter(t);
    legC.z -= 0.12f * t.crouch;
    const RayCollision legs = GetRayCollisionBox(
        ray, MakeBox(legC, Vector3{hitbox::kLegWidth + 0.08f * t.crouch, LegHeight(t), hitbox::kLegDepth + 0.25f * t.crouch}));
    // Arms and weapon held in front of the chest count as body.
    const float top = t.pos.y + LegHeight(t) + hitbox::kTorsoHeight;
    const RayCollision arms = GetRayCollisionBox(
        ray, MakeBox(Vector3{t.pos.x, top - 0.22f, t.pos.z - 0.3f}, Vector3{0.46f, 0.24f, 0.32f}));
    float best = 1e30f;
    if (head.hit && head.distance < best) { best = head.distance; result.hit = true; result.head = true; }
    if (torso.hit && torso.distance < best) { best = torso.distance; result.hit = true; result.head = false; result.legs = false; }
    if (arms.hit && arms.distance < best) { best = arms.distance; result.hit = true; result.head = false; result.legs = false; }
    if (legs.hit && legs.distance < best) { best = legs.distance; result.hit = true; result.head = false; result.legs = true; }
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

namespace {

Vector3 Add(Vector3 a, Vector3 b) { return Vector3{a.x + b.x, a.y + b.y, a.z + b.z}; }
Vector3 Scale3(Vector3 a, float k) { return Vector3{a.x * k, a.y * k, a.z * k}; }

// Two-bone IK in the leg's plane: knee position for a hip, a foot and the
// bone lengths, bending towards 'bendDir' (unit, forward for knees).
Vector3 SolveKnee(Vector3 hip, Vector3 foot, float upper, float lower, Vector3 bendDir) {
    Vector3 d = {foot.x - hip.x, foot.y - hip.y, foot.z - hip.z};
    float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const float maxLen = (upper + lower) * 0.999f;
    if (len > maxLen) {
        d = Scale3(d, maxLen / len);
        len = maxLen;
    }
    if (len < 1e-4f) return Add(hip, Scale3(bendDir, upper));
    const Vector3 dir = Scale3(d, 1.0f / len);
    // Distance along hip->foot to the knee's projection, and the knee's height off that line.
    const float a = (upper * upper - lower * lower + len * len) / (2.0f * len);
    const float h = std::sqrt(std::max(0.0f, upper * upper - a * a));
    // Bend direction made perpendicular to the leg.
    const float dot = bendDir.x * dir.x + bendDir.y * dir.y + bendDir.z * dir.z;
    Vector3 perp = {bendDir.x - dir.x * dot, bendDir.y - dir.y * dot, bendDir.z - dir.z * dot};
    const float pl = std::sqrt(perp.x * perp.x + perp.y * perp.y + perp.z * perp.z);
    perp = pl > 1e-5f ? Scale3(perp, 1.0f / pl) : bendDir;
    return Add(Add(hip, Scale3(dir, a)), Scale3(perp, h));
}

}  // namespace

// A Valorant-proportioned agent built from rounded parts. Its silhouette
// stays inside the hitboxes (head sphere, torso box, legs box), so what you
// see is what you hit.
void DrawAgent(const World& world, const Target& t, Color c) {
    const double yr = val::DegToRad(t.yaw);
    // Local axes in world space: forward = facing direction, right, up.
    const Vector3 fwd = {static_cast<float>(std::sin(yr)), 0.0f, static_cast<float>(-std::cos(yr))};
    const Vector3 right = {static_cast<float>(std::cos(yr)), 0.0f, static_cast<float>(std::sin(yr))};
    const Vector3 up = {0.0f, 1.0f, 0.0f};
    auto P = [&](float x, float y, float z) {  // local (right, up, forward) -> world
        return Vector3{t.pos.x + right.x * x + fwd.x * z, t.pos.y + y, t.pos.z + right.z * x + fwd.z * z};
    };

    const Color suit = Mix(Shade(c, 0.42f), Color{38, 40, 48, 255}, 0.35f);
    const Color armor = Shade(c, 0.95f);
    const Color skin = Mix(c, Color{255, 255, 255, 255}, 0.18f);
    const Color dark = Color{24, 26, 32, 255};
    const float legH = LegHeight(t);
    const float cr = t.crouch;

    // --- legs (two-bone IK, walk cycle) -------------------------------------
    const float stride = std::min(1.0f, t.moveSpeed / 5.4f);
    const float thigh = 0.46f, shin = 0.44f;
    for (int side = -1; side <= 1; side += 2) {
        const float ph = t.walkPhase + (side > 0 ? 3.14159265f : 0.0f);
        const float swing = std::sin(ph) * 0.32f * stride;
        const float lift = std::max(0.0f, std::cos(ph)) * 0.14f * stride + t.airborne * 0.22f;
        const float sx = static_cast<float>(side) * (0.1f + 0.05f * cr);
        const Vector3 hip = P(sx, legH, -0.02f);
        const Vector3 foot = P(sx * (1.0f + 0.4f * cr), 0.07f + lift, swing + 0.12f * cr);
        const Vector3 knee = SolveKnee(hip, foot, thigh, shin, fwd);
        world.DrawCapsuleLit(hip, knee, 0.085f, suit);
        world.DrawCapsuleLit(knee, foot, 0.07f, suit);
        world.DrawCapsuleLit(knee, knee, 0.078f, armor);  // knee pad
        world.DrawBoxYaw(Add(foot, Vector3{fwd.x * 0.05f, -0.035f, fwd.z * 0.05f}), Vector3{0.11f, 0.08f, 0.24f}, t.yaw, dark,
                         Surface::Cloth);
    }

    // --- torso ----------------------------------------------------------------
    const float lean = 0.08f * cr;  // crouching leans the chest forward a little
    const float torsoTop = legH + hitbox::kTorsoHeight;
    world.DrawCapsuleLit(P(0.0f, legH + 0.02f, 0.0f), P(0.0f, legH + 0.14f, lean * 0.3f), 0.125f, suit);  // hips
    world.DrawBoxAxes(P(0.0f, legH + 0.26f, lean * 0.5f), Vector3{0.36f, 0.22f, 0.24f}, right, up, fwd, suit, Surface::Cloth);
    world.DrawBoxAxes(P(0.0f, legH + 0.46f, lean), Vector3{0.46f, 0.28f, 0.28f}, right, up, fwd, armor, Surface::Cloth);  // chest rig
    world.DrawBoxAxes(P(0.0f, legH + 0.44f, lean + 0.145f), Vector3{0.3f, 0.16f, 0.02f}, right, up, fwd, Shade(armor, 0.7f),
                      Surface::Cloth);  // plate
    for (int side = -1; side <= 1; side += 2) {
        world.DrawCapsuleLit(P(static_cast<float>(side) * 0.16f, torsoTop - 0.08f, lean), P(static_cast<float>(side) * 0.18f, torsoTop - 0.1f, lean),
                             0.08f, armor);  // shoulders
    }
    world.DrawCapsuleLit(P(0.0f, torsoTop - 0.06f, lean), P(0.0f, torsoTop + 0.04f, lean * 0.8f), 0.055f, skin);  // neck

    // --- arms holding a pistol in front of the chest --------------------------
    const Vector3 hands = P(0.0f, torsoTop - 0.2f, 0.4f + lean);
    for (int side = -1; side <= 1; side += 2) {
        const float sx = static_cast<float>(side);
        const Vector3 shoulder = P(sx * 0.21f, torsoTop - 0.12f, lean);
        const Vector3 elbow = P(sx * 0.2f, torsoTop - 0.3f, 0.2f + lean);
        world.DrawCapsuleLit(shoulder, elbow, 0.058f, suit);
        world.DrawCapsuleLit(elbow, Add(hands, Scale3(right, sx * 0.035f)), 0.05f, suit);
    }
    world.DrawCapsuleLit(hands, hands, 0.055f, dark);  // gloves
    world.DrawBoxAxes(Add(hands, Add(Scale3(fwd, 0.1f), Vector3{0.0f, 0.04f, 0.0f})), Vector3{0.045f, 0.08f, 0.2f}, right, up, fwd,
                      Color{40, 42, 48, 255}, Surface::Metal);  // pistol

    // --- head -----------------------------------------------------------------
    const Vector3 head = HeadCenter(t);
    world.DrawSphereLit(head, hitbox::kHeadRadius * 0.97f, skin, Surface::Target);
    // Hair / helmet shell (slightly behind and above) and a visor band.
    world.DrawSphereLit(Add(head, Vector3{-fwd.x * 0.02f, 0.03f, -fwd.z * 0.02f}), hitbox::kHeadRadius * 0.9f, Shade(suit, 0.9f),
                        Surface::Cloth);
    world.DrawBoxAxes(Add(head, Vector3{fwd.x * 0.085f, 0.01f, fwd.z * 0.085f}), Vector3{0.2f, 0.055f, 0.08f}, right, up, fwd,
                      Color{20, 22, 28, 255}, Surface::Metal);

    world.DrawBlobShadow(t.pos, 0.42f, 0.4f * (1.0f - 0.5f * t.airborne));
}

void DrawTarget(const World& world, const Target& t, Color base) {
    Color c = base;
    if (t.beingHit) c = Mix(base, Color{255, 255, 255, 255}, 0.45f);
    if (t.hitFlash > 0.0) c = Mix(c, Color{255, 255, 255, 255}, 0.7f);

    if (t.kind == TargetKind::Sphere) {
        world.DrawSphereLit(t.pos, t.radius, c);
        return;
    }
    DrawAgent(world, t, c);
}
