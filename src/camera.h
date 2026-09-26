// camera.h - Valorant-accurate sensitivity math and the first-person camera.
#pragma once

#include <deque>

#include "raylib.h"

namespace val {

// Valorant turns 0.07 degrees per mouse count at in-game sensitivity 1.0.
constexpr double kDegreesPerCount = 0.07;

// Valorant's 103 degree horizontal FOV is defined for a 16:9 screen. With
// Hor+ scaling the *vertical* FOV stays fixed and the horizontal FOV grows or
// shrinks with the aspect ratio (so 16:9 shows exactly 103 degrees).
constexpr double kReferenceHorizontalFov = 103.0;
constexpr double kReferenceAspect = 16.0 / 9.0;

constexpr double kPitchLimit = 89.0;
constexpr double kPi = 3.14159265358979323846;

inline double DegToRad(double d) { return d * kPi / 180.0; }
inline double RadToDeg(double r) { return r * 180.0 / kPi; }

// Degrees turned for one mouse count.
double DegreesPerCount(double sens);
// eDPI = DPI x sens.
double Edpi(double dpi, double sens);
// Centimetres of mouse travel for a full 360 degree turn.
//   counts per 360 = 360 / (sens x 0.07);  inches = counts / DPI;  cm = inches x 2.54
double Cm360(double dpi, double sens);
// Inverse of Cm360(): the sens that gives 'cm' per 360 at 'dpi'.
double SensFromCm360(double dpi, double cm);
// Vertical FOV (degrees) that corresponds to 'hFov' at aspect 'aspect'.
double VerticalFovFromHorizontal(double hFovDeg, double aspect);
// Horizontal FOV (degrees) for a vertical FOV at aspect 'aspect'.
double HorizontalFovFromVertical(double vFovDeg, double aspect);
// Fixed Hor+ vertical FOV (about 70.53 degrees).
double GameVerticalFov();
// Horizontal FOV actually shown on a screen with the given aspect ratio.
double GameHorizontalFov(double aspect);
// Scoped sensitivity: Valorant divides the turn rate by the zoom, then applies
// the "Scoped Sensitivity Multiplier" (1.0 keeps the same on-screen speed).
double ScopedSens(double sens, double scopedMultiplier, double zoom);
// Vertical FOV while zoomed (zoom = magnification, e.g. 2.5 for Operator).
double ZoomedVerticalFov(double zoom);
// Wraps an angle into (-180, 180].
double NormalizeDeg(double deg);

}  // namespace val

// Short history of view angles, used to find the direction the crosshair was
// moving when a shot was fired (overshoot / undershoot) and to detect the
// moment a flick starts (reaction time).
class AimHistory {
public:
    void Push(double t, double yaw, double pitch);
    void Clear();
    // View-angle change over the last 'window' seconds. Returns false if there
    // is not enough history.
    bool DeltaOver(double now, double window, double& dYaw, double& dPitch) const;
    // Angular speed in degrees/second over the last 'window' seconds.
    double Speed(double now, double window) const;

private:
    struct Sample {
        double t;
        double yaw;
        double pitch;
    };
    std::deque<Sample> samples_;
};

// First-person camera driven purely by raw mouse counts. No smoothing, no
// acceleration: each count turns exactly sens x 0.07 degrees.
class ValCamera {
public:
    static constexpr float kEyeHeight = 1.6f;  // metres

    // Resets the view angles and puts the eye back at the default position.
    void Reset(double yawDeg = 0.0, double pitchDeg = 0.0);
    // Moves the eye (VS Bot mode walks around; every other mode stands still).
    void SetEye(Vector3 eye) { eye_ = eye; }
    // dx > 0 turns right, dy > 0 (mouse pulled toward you) looks down.
    void ApplyCounts(long dx, long dy, double sens);

    double Yaw() const { return yaw_; }
    double Pitch() const { return pitch_; }
    Vector3 Eye() const;
    Vector3 Forward() const;
    Ray AimRay() const;
    Camera3D ToRaylib(double zoom = 1.0) const;

private:
    Vector3 eye_ = {0.0f, kEyeHeight, 0.0f};
    double yaw_ = 0.0;    // degrees, 0 = looking down -Z, positive = right
    double pitch_ = 0.0;  // degrees, positive = up
};

// Direction helpers (degrees).
Vector3 DirectionFromAngles(double yawDeg, double pitchDeg);
void AnglesFromDirection(Vector3 dir, double& yawDeg, double& pitchDeg);
// Angle between the view ray and a world point, in degrees.
double AngleToPoint(const ValCamera& cam, Vector3 point);
