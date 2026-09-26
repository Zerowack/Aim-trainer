// world.cpp - range geometry and the lighting shader.
#include "world.h"

#include <algorithm>
#include <cfloat>

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

// Lighting: sky/ground ambient + one key light + rim, optional specular and
// glow, optional anti-aliased grid lines, exponential distance fog.
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
out vec4 finalColor;

float gridLine(vec2 p, float cell, float width) {
    vec2 q = p / cell;
    vec2 d = abs(fract(q - 0.5) - 0.5) / max(fwidth(q), vec2(1e-4));
    return 1.0 - min(min(d.x, d.y) / width, 1.0);
}

void main() {
    vec3 n = normalize(fragNormal);
    vec3 base = colDiffuse.rgb;

    if (grid > 0.5) {
        // Pick the plane that matches the face orientation.
        vec3 an = abs(n);
        vec2 p = an.y > 0.5 ? fragPos.xz : (an.x > 0.5 ? fragPos.zy : fragPos.xy);
        float minor = gridLine(p, gridCell, 1.0);
        float major = gridLine(p, gridCell * 5.0, 1.6);
        base = mix(base, gridColor, max(minor * 0.35, major * 0.75));
    }

    vec3 lightDir = normalize(vec3(0.35, 0.9, 0.45));
    float diffuse = max(dot(n, lightDir), 0.0);
    vec3 v = normalize(viewPos - fragPos);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 h = normalize(lightDir + v);
    float spec = pow(max(dot(n, h), 0.0), 40.0) * specular;
    float ambient = mix(0.30, 0.48, n.y * 0.5 + 0.5);

    vec3 c = base * (ambient + 0.62 * diffuse) + base * rim * 0.28 + vec3(spec) + base * emissive;
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
    sphere_ = LoadModelFromMesh(GenMeshSphere(1.0f, 32, 32));
    cube_ = LoadModelFromMesh(GenMeshCube(1.0f, 1.0f, 1.0f));
    sphere_.materials[0].shader = shader_;
    cube_.materials[0].shader = shader_;
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
    UnloadModel(sphere_);
    UnloadModel(cube_);
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

void World::UseSurface(Surface s, float gridCell) const {
    if (static_cast<int>(s) == currentSurface_ && gridCell == currentCell_) return;
    currentSurface_ = static_cast<int>(s);
    currentCell_ = gridCell;
    float grid = 0.0f, spec = 0.0f, emissive = 0.0f, fogAmount = 1.0f;
    switch (s) {
        case Surface::Plain: break;
        case Surface::Grid: grid = 1.0f; break;
        case Surface::Target: spec = 0.45f; emissive = 0.18f; fogAmount = 0.2f; break;
        case Surface::Glow: emissive = 0.9f; fogAmount = 0.1f; break;
    }
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

void World::DrawBoxLit(Vector3 center, Vector3 size, Color color, Surface s) const {
    UseSurface(s, currentCell_ > 0.0f ? currentCell_ : 1.0f);
    DrawModelEx(cube_, center, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, size, color);
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
    const Color c = Shade(Color{104, 94, 84, 255}, brightness_);
    const Color edge = Shade(Color{210, 64, 76, 255}, brightness_);
    for (const Box& box : covers_) {
        DrawBoxLit(box.center, box.size, c, Surface::Plain);
        // Thin red rim around the top so the edges are easy to read.
        const float top = box.center.y + box.size.y * 0.5f + 0.015f;
        const float rim = std::min(0.07f, std::min(box.size.x, box.size.z) * 0.25f);
        const float hx = box.size.x * 0.5f - rim * 0.5f, hz = box.size.z * 0.5f - rim * 0.5f;
        DrawBoxLit(Vector3{box.center.x, top, box.center.z - hz}, Vector3{box.size.x, 0.03f, rim}, edge, Surface::Plain);
        DrawBoxLit(Vector3{box.center.x, top, box.center.z + hz}, Vector3{box.size.x, 0.03f, rim}, edge, Surface::Plain);
        DrawBoxLit(Vector3{box.center.x - hx, top, box.center.z}, Vector3{rim, 0.03f, box.size.z}, edge, Surface::Plain);
        DrawBoxLit(Vector3{box.center.x + hx, top, box.center.z}, Vector3{rim, 0.03f, box.size.z}, edge, Surface::Plain);
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
