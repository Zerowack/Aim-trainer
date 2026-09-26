// movement.cpp - Valorant-style character movement (see movement.h).
#include "movement.h"

#include <algorithm>
#include <cmath>

#include "camera.h"

namespace mv {

float Mover::Speed() const { return std::sqrt(vel.x * vel.x + vel.z * vel.z); }

void Accelerate(Mover& m, float wishX, float wishZ, float wishSpeed, float dt) {
    if (!m.onGround) {
        m.vel.x += wishX * kAirAccel * dt;
        m.vel.z += wishZ * kAirAccel * dt;
        return;
    }
    if (wishSpeed > 0.0f && (wishX != 0.0f || wishZ != 0.0f)) {
        const float dvx = wishX * wishSpeed - m.vel.x;
        const float dvz = wishZ * wishSpeed - m.vel.z;
        const float len = std::sqrt(dvx * dvx + dvz * dvz);
        const float maxDv = kAccel * dt;
        const float k = len > maxDv ? maxDv / len : 1.0f;
        m.vel.x += dvx * k;
        m.vel.z += dvz * k;
    } else {
        const float sp = m.Speed();
        if (sp > 0.0f) {
            const float ns = std::max(0.0f, sp - kFriction * dt);
            m.vel.x *= ns / sp;
            m.vel.z *= ns / sp;
        }
    }
}

void Collide(Mover& m, const std::vector<Box>& boxes) {
    for (int pass = 0; pass < 3; ++pass) {
        for (const Box& b : boxes) {
            const float hx = b.size.x * 0.5f, hz = b.size.z * 0.5f;
            const float cx = std::max(b.center.x - hx, std::min(m.pos.x, b.center.x + hx));
            const float cz = std::max(b.center.z - hz, std::min(m.pos.z, b.center.z + hz));
            const float dx = m.pos.x - cx, dz = m.pos.z - cz;
            const float d2 = dx * dx + dz * dz;
            if (d2 >= kRadius * kRadius) continue;
            if (d2 > 1e-8f) {
                const float d = std::sqrt(d2);
                const float push = kRadius - d;
                m.pos.x += dx / d * push;
                m.pos.z += dz / d * push;
                // Remove the velocity component going into the box.
                const float nx = dx / d, nz = dz / d;
                const float vn = m.vel.x * nx + m.vel.z * nz;
                if (vn < 0.0f) {
                    m.vel.x -= vn * nx;
                    m.vel.z -= vn * nz;
                }
            } else {
                // Centre inside the box: push out along the shallowest axis.
                const float ox = hx + kRadius - std::fabs(m.pos.x - b.center.x);
                const float oz = hz + kRadius - std::fabs(m.pos.z - b.center.z);
                if (ox < oz) m.pos.x += (m.pos.x >= b.center.x ? ox : -ox);
                else m.pos.z += (m.pos.z >= b.center.z ? oz : -oz);
            }
        }
    }
}

void Vertical(Mover& m, float dt) {
    if (m.onGround) return;
    m.vel.y -= kGravity * dt;
    m.pos.y += m.vel.y * dt;
    if (m.pos.y <= 0.0f) {
        m.pos.y = 0.0f;
        m.vel.y = 0.0f;
        m.onGround = true;
    }
}

void Crouch(Mover& m, bool down, float dt) {
    const float target = down ? 1.0f : 0.0f;
    m.crouch += std::max(-kCrouchRate * dt, std::min(kCrouchRate * dt, target - m.crouch));
}

void Step(Mover& m, float wishX, float wishZ, float wishSpeed, bool crouch, float dt, const std::vector<Box>& boxes) {
    Crouch(m, crouch, dt);
    if (m.crouch > 0.5f) wishSpeed = std::min(wishSpeed, kCrouchSpeed);
    Accelerate(m, wishX, wishZ, wishSpeed, dt);
    m.pos.x += m.vel.x * dt;
    m.pos.z += m.vel.z * dt;
    Vertical(m, dt);
    Collide(m, boxes);
}

void StepPlayer(Mover& m, double yawDeg, bool live, float speedMult, float dt, const std::vector<Box>& boxes) {
    const bool crouchKey = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    Crouch(m, crouchKey, dt);

    float wx = 0.0f, wz = 0.0f, speed = 0.0f;
    if (live) {
        const double y = val::DegToRad(yawDeg);
        const float fx = static_cast<float>(std::sin(y)), fz = static_cast<float>(-std::cos(y));
        const float rx = static_cast<float>(std::cos(y)), rz = static_cast<float>(std::sin(y));
        float ix = 0.0f, iz = 0.0f;
        if (IsKeyDown(KEY_W)) { ix += fx; iz += fz; }
        if (IsKeyDown(KEY_S)) { ix -= fx; iz -= fz; }
        if (IsKeyDown(KEY_D)) { ix += rx; iz += rz; }
        if (IsKeyDown(KEY_A)) { ix -= rx; iz -= rz; }
        const float len = std::sqrt(ix * ix + iz * iz);
        if (len > 0.001f) {
            wx = ix / len;
            wz = iz / len;
            const bool walk = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            speed = (m.crouch > 0.5f ? kCrouchSpeed : (walk ? kWalkSpeed : kRunSpeed)) * speedMult;
        }
        if (IsKeyPressed(KEY_SPACE) && m.onGround) {
            m.onGround = false;
            m.vel.y = kJumpSpeed;
        }
    }
    Accelerate(m, wx, wz, speed, dt);
    m.pos.x += m.vel.x * dt;
    m.pos.z += m.vel.z * dt;
    Vertical(m, dt);
    Collide(m, boxes);
}

void CounterStrafe(const Mover& m, float& wishX, float& wishZ, float& wishSpeed) {
    const float sp = m.Speed();
    if (sp > 0.3f) {
        wishX = -m.vel.x / sp;
        wishZ = -m.vel.z / sp;
        wishSpeed = 0.01f;  // accelerate against the motion, but don't run the other way
    }
}

Target BodyOf(const Mover& m) {
    Target t;
    t.kind = TargetKind::Humanoid;
    t.pos = m.pos;
    t.crouch = m.crouch;
    t.presented = true;
    return t;
}

bool CanSee(const World& world, Vector3 from, const Target& body) {
    const Vector3 pts[2] = {TargetAimPoint(body, true), TargetAimPoint(body, false)};
    for (const Vector3& p : pts) {
        const Vector3 d = Sub(p, from);
        const float len = Len(d);
        if (len < 1e-3f) return true;
        const Ray ray = {from, Vector3{d.x / len, d.y / len, d.z / len}};
        if (world.RaycastCovers(ray) > len) return true;
    }
    return false;
}

Vector3 Sub(Vector3 a, Vector3 b) { return Vector3{a.x - b.x, a.y - b.y, a.z - b.z}; }
float Len(Vector3 v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

double RandNormal(Rng& rng) {
    const double u1 = std::max(1e-9, rng.Uniform(0.0, 1.0));
    const double u2 = rng.Uniform(0.0, 1.0);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * val::kPi * u2);
}

}  // namespace mv
