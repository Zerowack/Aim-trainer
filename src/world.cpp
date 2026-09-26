// world.cpp - range geometry and the lighting shader.
#include "world.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "rlgl.h"

namespace {

// OpenGL 3.3 shaders (raylib's default on Windows).
const char* const kVertexShader = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Lighting: sky/ground ambient + key and fill lights + rim, optional
// specular and glow, procedural surface patterns (grid, concrete, floor
// tiles, crates), soft darkening near the floor, exponential distance fog.
const char* const kFragmentShader = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 fogColor;
uniform float fogDensity;
uniform float fogAmount;
uniform float grid;
uniform float gridCell;
uniform vec3 gridColor;
uniform float specular;
uniform float emissive;
uniform float pattern;     // 0 none, 2 concrete, 3 floor tiles, 4 crate
uniform vec3 boxCenter;
uniform vec3 boxHalf;
uniform float shadowMode;
out vec4 finalColor;

float gridLine(vec2 p, float cell, float width) {
    vec2 q = p / cell;
    vec2 d = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), vec2(1e-4));
    return 1.0 - min(min(d.x, d.y) / width, 1.0);
}
float line1(float v, float cell, float width) {
    float q = v / cell;
    float d = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), 1e-4);
    return 1.0 - min(d / width, 1.0);
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), u.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), u.x), u.y);
}
// Distance from the fragment to the nearest edge of the current box face.
float edgeDistance(vec3 an) {
    vec3 l = abs(fragPos - boxCenter);
    vec3 e = boxHalf - l;
    if (an.x > 0.5) return min(e.y, e.z);
    if (an.y > 0.5) return min(e.x, e.z);
    return min(e.x, e.y);
}

void main() {
    if (shadowMode > 0.5) {
        finalColor = vec4(0.0, 0.0, 0.0, colDiffuse.a);
        return;
    }
    vec3 n = normalize(fragNormal);
    vec3 an = abs(n);
    vec3 base = colDiffuse.rgb;
    vec2 p = an.y > 0.5 ? fragPos.xz : (an.x > 0.5 ? fragPos.zy : fragPos.xy);

    if (grid > 0.5) {
        float minor = gridLine(p, gridCell, 1.0);
        float major = gridLine(p, gridCell * 5.0, 1.6);
        base = mix(base, gridColor, max(minor * 0.35, major * 0.75));
    }
    if (pattern > 1.5 && pattern < 2.5) {
        // Concrete: grain, panel seams, bevelled edges, darker near the floor.
        base *= 0.88 + 0.14 * noise(p * 2.5) + 0.06 * noise(p * 17.0);
        if (an.y < 0.5) {
            float seam = max(line1(p.x, 2.0, 1.1), line1(p.y, 1.25, 1.1));
            base *= 1.0 - 0.22 * seam;
        }
        float e = edgeDistance(an);
        base *= 1.0 + 0.16 * (1.0 - smoothstep(0.0, 0.04, e));
    } else if (pattern > 2.5 && pattern < 3.5) {
        // Floor tiles (2 m) with per-tile variation and seams.
        vec2 tile = floor(p / 2.0);
        base *= 0.9 + 0.1 * hash(tile) + 0.05 * noise(p * 6.0);
        base *= 1.0 - 0.3 * gridLine(p, 2.0, 1.2);
    } else if (pattern > 3.5) {
        // Crate: lighter frame around every face, planks inside.
        float e = edgeDistance(an);
        float frame = 1.0 - smoothstep(0.075, 0.09, e);
        vec2 q = an.y > 0.5 ? p : vec2(p.x, p.y * 1.0);
        float plank = line1(q.y, 0.3, 1.0);
        vec3 inner = base * (0.82 + 0.1 * noise(q * vec2(1.5, 12.0))) * (1.0 - 0.25 * plank);
        base = mix(inner, base * 1.2, frame);
        base *= 1.0 + 0.2 * (1.0 - smoothstep(0.0, 0.025, e));
    }
    if (pattern > 1.5 && an.y < 0.5) {
        base *= mix(0.58, 1.0, smoothstep(0.0, 0.9, fragPos.y));  // contact shadow
    }

    vec3 lightDir = normalize(vec3(0.35, 0.9, 0.45));
    vec3 fillDir = normalize(vec3(-0.6, 0.35, -0.5));
    float diffuse = max(dot(n, lightDir), 0.0);
    float fill = max(dot(n, fillDir), 0.0);
    vec3 v = normalize(viewPos - fragPos);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 h = normalize(lightDir + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * specular;
    float ambient = mix(0.28, 0.46, n.y * 0.5 + 0.5);

    vec3 c = base * (ambient + 0.6 * diffuse + 0.16 * fill) + base * rim * 0.28 + vec3(spec) + base * emissive;
    float dist = length(fragPos - viewPos);
    float fog = (1.0 - exp(-dist * fogDensity)) * fogAmount;
    finalColor = vec4(mix(c, fogColor, clamp(fog, 0.0, 1.0)), colDiffuse.a);
}
)";

unsigned char ClampByte(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<unsigned char>(v);
}

const Color kSky = {22, 25, 32, 255};

}  // namespace

Color Shade(Color c, float k) {
    return Color{ClampByte(c.r * k), ClampByte(c.g * k), ClampByte(c.b * k), c.a};
}

bool World::Init() {
    shader_ = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    locViewPos_ = GetShaderLocation(shader_, "viewPos");
    locFogColor_ = GetShaderLocation(shader_, "fogColor");
    locFogDensity_ = GetShaderLocation(shader_, "fogDensity");
    locFogAmount_ = GetShaderLocation(shader_, "fogAmount");
    locGrid_ = GetShaderLocation(shader_, "grid");
    locGridCell_ = GetShaderLocation(shader_, "gridCell");
    locGridColor_ = GetShaderLocation(shader_, "gridColor");
    locSpecular_ = GetShaderLocation(shader_, "specular");
    locEmissive_ = GetShaderLocation(shader_, "emissive");
    locPattern_ = GetShaderLocation(shader_, "pattern");
    locBoxCenter_ = GetShaderLocation(shader_, "boxCenter");
    locBoxHalf_ = GetShaderLocation(shader_, "boxHalf");
    locShadow_ = GetShaderLocation(shader_, "shadowMode");
    sphere_ = LoadModelFromMesh(GenMeshSphere(1.0f, 32, 32));
    cube_ = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    cylinder_ = LoadModelFromMesh(GenMeshCylinder(1.0f, 1.0f, 20));
    disc_ = LoadModelFromMesh(GenMeshCylinder(1.0f, 0.01f, 28));
    sphere_.materials[0].shader = shader_;
    cube_.materials[0].shader = shader_;
    cylinder_.materials[0].shader = shader_;
    disc_.materials[0].shader = shader_;
    ready_ = IsShaderValid(shader_);
    return ready_;
}

void World::Shutdown() {
    // Detach the shared shader before unloading the models so it is only
    // unloaded once.
    Shader defaultShader = {};
    defaultShader.id = rlGetShaderIdDefault();
    defaultShader.locs = rlGetShaderLocsDefault();
    sphere_.materials[0].shader = defaultShader;
    cube_.materials[0].shader = defaultShader;
    cylinder_.materials[0].shader = defaultShader;
    disc_.materials[0].shader = defaultShader;
    UnloadModel(sphere_);
    UnloadModel(cube_);
    UnloadModel(cylinder_);
    UnloadModel(disc_);
    UnloadShader(shader_);
    ready_ = false;
}

Color World::SkyColor(float brightness) const { return Shade(kSky, brightness); }

void World::BeginFrame(Vector3 eye, float brightness) {
    brightness_ = brightness;
    const float pos[3] = {eye.x, eye.y, eye.z};
    SetShaderValue(shader_, locViewPos_, pos, SHADER_UNIFORM_VEC3);
    const Color sky = SkyColor(brightness);
    const float fog[3] = {sky.r / 255.0f, sky.g / 255.0f, sky.b / 255.0f};
    SetShaderValue(shader_, locFogColor_, fog, SHADER_UNIFORM_VEC3);
    const float density = 0.018f;
    SetShaderValue(shader_, locFogDensity_, &density, SHADER_UNIFORM_FLOAT);
    const Color g = Shade(Color{92, 98, 116, 255}, brightness);
    const float gridColor[3] = {g.r / 255.0f, g.g / 255.0f, g.b / 255.0f};
    SetShaderValue(shader_, locGridColor_, gridColor, SHADER_UNIFORM_VEC3);
    currentSurface_ = -1;
    currentCell_ = -1.0f;
}

void World::SetFog(Color color, float density) const {
    const float fog[3] = {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
    SetShaderValue(shader_, locFogColor_, fog, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, locFogDensity_, &density, SHADER_UNIFORM_FLOAT);
}

void World::UseSurface(Surface s, float gridCell) const {
    if (static_cast<int>(s) == currentSurface_ && gridCell == currentCell_) return;
    currentSurface_ = static_cast<int>(s);
    currentCell_ = gridCell;
    float grid = 0.0f, spec = 0.0f, emissive = 0.0f, fogAmount = 1.0f, pattern = 0.0f, shadow = 0.0f;
    switch (s) {
        case Surface::Plain: break;
        case Surface::Grid: grid = 1.0f; break;
        case Surface::Target: spec = 0.45f; emissive = 0.18f; fogAmount = 0.2f; break;
        case Surface::Glow: emissive = 0.9f; fogAmount = 0.1f; break;
        case Surface::Wall: pattern = 2.0f; break;
        case Surface::Floor: pattern = 3.0f; break;
        case Surface::Crate: pattern = 4.0f; spec = 0.08f; break;
        case Surface::Metal: spec = 0.7f; fogAmount = 0.3f; break;
        case Surface::Cloth: spec = 0.12f; emissive = 0.1f; fogAmount = 0.2f; break;
        case Surface::Shadow: shadow = 1.0f; break;
    }
    SetShaderValue(shader_, locPattern_, &pattern, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locShadow_, &shadow, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locGrid_, &grid, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locGridCell_, &gridCell, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locSpecular_, &spec, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locEmissive_, &emissive, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, locFogAmount_, &fogAmount, SHADER_UNIFORM_FLOAT);
}

void World::DrawSphereLit(Vector3 center, float radius, Color color, Surface s) const {
    UseSurface(s);
    DrawModelEx(sphere_, center, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{radius, radius, radius}, color);
}

void World::SetBoxFrame(Vector3 center, Vector3 half) const {
    const float c[3] = {center.x, center.y, center.z};
    const float h[3] = {half.x, half.y, half.z};
    SetShaderValue(shader_, locBoxCenter_, c, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader_, locBoxHalf_, h, SHADER_UNIFORM_VEC3);
}

void World::DrawBoxLit(Vector3 center, Vector3 size, Color color, Surface s) const {
    UseSurface(s, currentCell_ > 0.0f ? currentCell_ : 1.0f);
    if (s == Surface::Wall || s == Surface::Crate) SetBoxFrame(center, Vector3{size.x * 0.5f, size.y * 0.5f, size.z * 0.5f});
    DrawModelEx(cube_, center, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, size, color);
}

void World::DrawBoxYaw(Vector3 center, Vector3 size, float yawDeg, Color color, Surface s) const {
    UseSurface(s, currentCell_ > 0.0f ? currentCell_ : 1.0f);
    // Camera yaw turns clockwise seen from above; raylib rotates counter-clockwise.
    DrawModelEx(cube_, center, Vector3{0.0f, 1.0f, 0.0f}, -yawDeg, size, color);
}

void World::DrawBoxAxes(Vector3 center, Vector3 size, Vector3 r, Vector3 u, Vector3 f, Color color, Surface s) const {
    UseSurface(s, currentCell_ > 0.0f ? currentCell_ : 1.0f);
    // Columns = the box axes scaled by its size (unit cube mesh), plus the centre.
    Matrix m = {r.x * size.x, u.x * size.y, f.x * size.z, center.x,
                r.y * size.x, u.y * size.y, f.y * size.z, center.y,
                r.z * size.x, u.z * size.y, f.z * size.z, center.z,
                0.0f,         0.0f,         0.0f,         1.0f};
    // DrawMesh uses the material colour: set it for this draw, then restore
    // it (DrawModelEx multiplies its tint by the stored colour).
    Color& stored = cube_.materials[0].maps[MATERIAL_MAP_DIFFUSE].color;
    const Color saved = stored;
    stored = color;
    DrawMesh(cube_.meshes[0], cube_.materials[0], m);
    stored = saved;
}

void World::DrawCapsuleLit(Vector3 a, Vector3 b, float radius, Color color, Surface s) const {
    UseSurface(s);
    const Vector3 d = {b.x - a.x, b.y - a.y, b.z - a.z};
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    DrawModelEx(sphere_, a, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{radius, radius, radius}, color);
    DrawModelEx(sphere_, b, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{radius, radius, radius}, color);
    if (len < 1e-4f) return;
    // Rotate the cylinder's +Y axis onto a->b.
    const Vector3 dir = {d.x / len, d.y / len, d.z / len};
    Vector3 axis = {dir.z, 0.0f, -dir.x};  // cross(up, dir)
    const float axisLen = std::sqrt(axis.x * axis.x + axis.z * axis.z);
    float angle = std::acos(std::max(-1.0f, std::min(1.0f, dir.y))) * RAD2DEG;
    if (axisLen < 1e-5f) {
        axis = Vector3{1.0f, 0.0f, 0.0f};
        angle = dir.y > 0.0f ? 0.0f : 180.0f;
    } else {
        axis.x /= axisLen;
        axis.z /= axisLen;
    }
    DrawModelEx(cylinder_, a, axis, angle, Vector3{radius, len, radius}, color);
}

void World::DrawBlobShadow(Vector3 feet, float radius, float strength) const {
    UseSurface(Surface::Shadow);
    const unsigned char a = static_cast<unsigned char>(std::max(0.0f, std::min(1.0f, strength)) * 255.0f);
    // Two discs: a soft wide one and a darker core.
    DrawModelEx(disc_, Vector3{feet.x, 0.006f, feet.z}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{radius, 1.0f, radius},
                Color{0, 0, 0, static_cast<unsigned char>(a / 2)});
    DrawModelEx(disc_, Vector3{feet.x, 0.008f, feet.z}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                Vector3{radius * 0.6f, 1.0f, radius * 0.6f}, Color{0, 0, 0, a});
}

void World::DrawArenaFloor(float halfX, float halfZ, Color color) const {
    UseSurface(Surface::Floor);
    DrawModelEx(cube_, Vector3{0.0f, -0.05f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f,
                Vector3{halfX * 2.0f + 8.0f, 0.1f, halfZ * 2.0f + 8.0f}, Shade(color, brightness_));
}

void World::DrawRange() const {
    // Room: 80 m x 80 m, player at the centre, eye height 1.6 m.
    const float b = brightness_;
    const Color floorC = Shade(Color{46, 50, 60, 255}, b);
    const Color wallC = Shade(Color{56, 61, 73, 255}, b);
    const Color sideC = Shade(Color{50, 55, 66, 255}, b);
    const Color pillarC = Shade(Color{70, 75, 90, 255}, b);
    const Color trimC = Shade(Color{36, 39, 47, 255}, b);
    const Color markC = Shade(Color{210, 64, 76, 255}, b);

    // Floor: 1 m grid with a stronger line every 5 m.
    UseSurface(Surface::Grid, 1.0f);
    DrawModelEx(cube_, Vector3{0.0f, -0.05f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{80.0f, 0.1f, 80.0f}, floorC);
    // Walls: 2 m panels.
    UseSurface(Surface::Grid, 2.0f);
    DrawModelEx(cube_, Vector3{0.0f, 7.0f, -40.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{80.0f, 14.0f, 0.5f}, wallC);
    DrawModelEx(cube_, Vector3{0.0f, 7.0f, 40.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{80.0f, 14.0f, 0.5f}, wallC);
    DrawModelEx(cube_, Vector3{-40.0f, 7.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{0.5f, 14.0f, 80.0f}, sideC);
    DrawModelEx(cube_, Vector3{40.0f, 7.0f, 0.0f}, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{0.5f, 14.0f, 80.0f}, sideC);

    // Dark skirting along the bottom of every wall.
    DrawBoxLit(Vector3{0.0f, 0.3f, -39.6f}, Vector3{80.0f, 0.6f, 0.4f}, trimC, Surface::Plain);
    DrawBoxLit(Vector3{0.0f, 0.3f, 39.6f}, Vector3{80.0f, 0.6f, 0.4f}, trimC, Surface::Plain);
    DrawBoxLit(Vector3{-39.6f, 0.3f, 0.0f}, Vector3{0.4f, 0.6f, 80.0f}, trimC, Surface::Plain);
    DrawBoxLit(Vector3{39.6f, 0.3f, 0.0f}, Vector3{0.4f, 0.6f, 80.0f}, trimC, Surface::Plain);

    // Distance markers in front of the player: 10, 20, 30 m.
    for (int d = 10; d <= 30; d += 10) {
        const float z = -static_cast<float>(d);
        DrawBoxLit(Vector3{0.0f, 0.006f, z}, Vector3{24.0f, 0.004f, 0.08f}, markC, Surface::Plain);
    }
    // Pillars along the walls give peripheral reference points; each has a
    // red accent band at eye height.
    for (int i = -3; i <= 3; ++i) {
        const float p = static_cast<float>(i) * 11.0f;
        const Vector3 spots[4] = {{p, 7.0f, -39.2f}, {p, 7.0f, 39.2f}, {-39.2f, 7.0f, p}, {39.2f, 7.0f, p}};
        for (const Vector3& s : spots) {
            DrawBoxLit(s, Vector3{1.2f, 14.0f, 1.2f}, pillarC, Surface::Plain);
            DrawBoxLit(Vector3{s.x, 2.4f, s.z}, Vector3{1.26f, 0.12f, 1.26f}, markC, Surface::Plain);
        }
    }
}

void World::DrawCovers() const {
    const Color edge = Shade(Color{210, 64, 76, 255}, brightness_);
    for (const Box& box : covers_) {
        const bool tinted = box.tint.a > 0;
        switch (box.style) {
            case CoverStyle::Wall: {
                const Color c = Shade(tinted ? box.tint : Color{150, 146, 138, 255}, brightness_);
                DrawBoxLit(box.center, box.size, c, Surface::Wall);
                break;
            }
            case CoverStyle::Crate: {
                const Color c = Shade(tinted ? box.tint : Color{150, 112, 72, 255}, brightness_);
                DrawBoxLit(box.center, box.size, c, Surface::Crate);
                break;
            }
            case CoverStyle::Barrier: {
                const Color c = Shade(tinted ? box.tint : Color{128, 132, 136, 255}, brightness_);
                DrawBoxLit(box.center, box.size, c, Surface::Wall);
                // Painted stripe along the top.
                const Color stripe = Shade(Color{226, 180, 60, 255}, brightness_);
                const float y = box.center.y + box.size.y * 0.5f - 0.09f;
                DrawBoxLit(Vector3{box.center.x, y, box.center.z}, Vector3{box.size.x + 0.01f, 0.08f, box.size.z + 0.01f}, stripe,
                           Surface::Plain);
                break;
            }
            default: {
                const Color c = Shade(tinted ? box.tint : Color{104, 94, 84, 255}, brightness_);
                DrawBoxLit(box.center, box.size, c, Surface::Plain);
                // Thin red rim around the top so the edges are easy to read.
                const float top = box.center.y + box.size.y * 0.5f + 0.015f;
                const float rim = std::min(0.07f, std::min(box.size.x, box.size.z) * 0.25f);
                const float hx = box.size.x * 0.5f - rim * 0.5f, hz = box.size.z * 0.5f - rim * 0.5f;
                DrawBoxLit(Vector3{box.center.x, top, box.center.z - hz}, Vector3{box.size.x, 0.03f, rim}, edge, Surface::Plain);
                DrawBoxLit(Vector3{box.center.x, top, box.center.z + hz}, Vector3{box.size.x, 0.03f, rim}, edge, Surface::Plain);
                DrawBoxLit(Vector3{box.center.x - hx, top, box.center.z}, Vector3{rim, 0.03f, box.size.z}, edge, Surface::Plain);
                DrawBoxLit(Vector3{box.center.x + hx, top, box.center.z}, Vector3{rim, 0.03f, box.size.z}, edge, Surface::Plain);
                break;
            }
        }
    }
}

float World::RaycastCovers(const Ray& ray) const {
    float best = FLT_MAX;
    for (const Box& box : covers_) {
        const Vector3 h = {box.size.x * 0.5f, box.size.y * 0.5f, box.size.z * 0.5f};
        const BoundingBox bb = {Vector3{box.center.x - h.x, box.center.y - h.y, box.center.z - h.z},
                                Vector3{box.center.x + h.x, box.center.y + h.y, box.center.z + h.z}};
        const RayCollision rc = GetRayCollisionBox(ray, bb);
        if (rc.hit && rc.distance < best) best = rc.distance;
    }
    return best;
}
