// camera.cpp - Valorant sensitivity/FOV math and the raw-input camera.
#include "camera.h"

#include <cmath>

namespace val {

double DegreesPerCount(double sens) { return sens * kDegreesPerCount; }

double Edpi(double dpi, double sens) { return dpi * sens; }

double Cm360(double dpi, double sens) {
    const double denom = dpi * sens * kDegreesPerCount;
    if (denom <= 0.0) return 0.0;
    return 360.0 / denom * 2.54;
}

double SensFromCm360(double dpi, double cm) {
    const double denom = dpi * kDegreesPerCount * cm;
    if (denom <= 0.0) return 0.0;
    return 360.0 * 2.54 / denom;
}

double VerticalFovFromHorizontal(double hFovDeg, double aspect) {
    return RadToDeg(2.0 * std::atan(std::tan(DegToRad(hFovDeg) * 0.5) / aspect));
}

double HorizontalFovFromVertical(double vFovDeg, double aspect) {
    return RadToDeg(2.0 * std::atan(std::tan(DegToRad(vFovDeg) * 0.5) * aspect));
}

double GameVerticalFov() {
    return VerticalFovFromHorizontal(kReferenceHorizontalFov, kReferenceAspect);
}

double GameHorizontalFov(double aspect) {
    return HorizontalFovFromVertical(GameVerticalFov(), aspect);
}

double NormalizeDeg(double deg) {
    deg = std::fmod(deg, 360.0);
    if (deg > 180.0) deg -= 360.0;
    if (deg <= -180.0) deg += 360.0;
    return deg;
}

}  // namespace val

// ---------------------------------------------------------------------------
// AimHistory

void AimHistory::Push(double t, double yaw, double pitch) {
    samples_.push_back({t, yaw, pitch});
    // Keep half a second of history - plenty for any query we make.
    while (!samples_.empty() && t - samples_.front().t > 0.5) samples_.pop_front();
}

void AimHistory::Clear() { samples_.clear(); }

bool AimHistory::DeltaOver(double now, double window, double& dYaw, double& dPitch) const {
    dYaw = 0.0;
    dPitch = 0.0;
    const size_t n = samples_.size();
    if (n == 0) return false;
    // Walk back to the oldest sample that is still inside the window.
    // (Indices, not pointers: std::deque storage is not contiguous.)
    size_t first = n;
    while (first > 0 && now - samples_[first - 1].t <= window) --first;
    if (first == n) return false;  // no movement inside the window
    // Use the sample just before the window if available, so a single
    // movement packet inside the window still produces a delta.
    const Sample& ref = samples_[first > 0 ? first - 1 : 0];
    const Sample& last = samples_[n - 1];
    dYaw = val::NormalizeDeg(last.yaw - ref.yaw);
    dPitch = last.pitch - ref.pitch;
    return true;
}

double AimHistory::Speed(double now, double window) const {
    double dy = 0.0, dp = 0.0;
    if (!DeltaOver(now, window, dy, dp) || window <= 0.0) return 0.0;
    return std::sqrt(dy * dy + dp * dp) / window;
}

// ---------------------------------------------------------------------------
// ValCamera

void ValCamera::Reset(double yawDeg, double pitchDeg) {
    yaw_ = val::NormalizeDeg(yawDeg);
    pitch_ = pitchDeg;
}

void ValCamera::ApplyCounts(long dx, long dy, double sens) {
    const double k = val::DegreesPerCount(sens);
    yaw_ = val::NormalizeDeg(yaw_ + static_cast<double>(dx) * k);
    pitch_ -= static_cast<double>(dy) * k;
    if (pitch_ > val::kPitchLimit) pitch_ = val::kPitchLimit;
    if (pitch_ < -val::kPitchLimit) pitch_ = -val::kPitchLimit;
}

Vector3 ValCamera::Eye() const { return Vector3{0.0f, kEyeHeight, 0.0f}; }

Vector3 ValCamera::Forward() const { return DirectionFromAngles(yaw_, pitch_); }

Ray ValCamera::AimRay() const { return Ray{Eye(), Forward()}; }

Camera3D ValCamera::ToRaylib() const {
    Camera3D c = {};
    const Vector3 eye = Eye();
    const Vector3 f = Forward();
    c.position = eye;
    c.target = Vector3{eye.x + f.x, eye.y + f.y, eye.z + f.z};
    c.up = Vector3{0.0f, 1.0f, 0.0f};
    c.fovy = static_cast<float>(val::GameVerticalFov());
    c.projection = CAMERA_PERSPECTIVE;
    return c;
}

Vector3 DirectionFromAngles(double yawDeg, double pitchDeg) {
    const double y = val::DegToRad(yawDeg);
    const double p = val::DegToRad(pitchDeg);
    return Vector3{static_cast<float>(std::sin(y) * std::cos(p)), static_cast<float>(std::sin(p)),
                   static_cast<float>(-std::cos(y) * std::cos(p))};
}

void AnglesFromDirection(Vector3 dir, double& yawDeg, double& pitchDeg) {
    const double horiz = std::sqrt(static_cast<double>(dir.x) * dir.x + static_cast<double>(dir.z) * dir.z);
    yawDeg = val::RadToDeg(std::atan2(static_cast<double>(dir.x), -static_cast<double>(dir.z)));
    pitchDeg = val::RadToDeg(std::atan2(static_cast<double>(dir.y), horiz));
}

double AngleToPoint(const ValCamera& cam, Vector3 point) {
    const Vector3 eye = cam.Eye();
    const double dx = point.x - eye.x, dy = point.y - eye.y, dz = point.z - eye.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len <= 1e-9) return 0.0;
    const Vector3 f = cam.Forward();
    double c = (dx * f.x + dy * f.y + dz * f.z) / len;
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    return val::RadToDeg(std::acos(c));
}
