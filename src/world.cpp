// world.cpp - range geometry and a tiny lighting shader.
#include "world.h"

#include <cfloat>

#include "rlgl.h"

namespace {

// Simple directional light + ambient + rim light. Written for OpenGL 3.3,
// which is what raylib uses on Windows by default.
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

const char* const kFragmentShader = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    vec3 lightDir = normalize(vec3(0.35, 0.9, 0.45));
    float diffuse = max(dot(n, lightDir), 0.0);
    vec3 v = normalize(viewPos - fragPos);
    float rim = pow(1.0 - max(dot(n, v), 0.0), 3.0);
    vec3 c = colDiffuse.rgb * (0.42 + 0.62 * diffuse) + colDiffuse.rgb * rim * 0.25;
    finalColor = vec4(c, colDiffuse.a);
}
)";

unsigned char ClampByte(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<unsigned char>(v);
}

}  // namespace

Color Shade(Color c, float k) {
    return Color{ClampByte(c.r * k), ClampByte(c.g * k), ClampByte(c.b * k), c.a};
}

bool World::Init() {
    shader_ = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    viewPosLoc_ = GetShaderLocation(shader_, "viewPos");
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

void World::SetViewPosition(Vector3 eye) {
    const float pos[3] = {eye.x, eye.y, eye.z};
    SetShaderValue(shader_, viewPosLoc_, pos, SHADER_UNIFORM_VEC3);
}

void World::DrawSphereLit(Vector3 center, float radius, Color color) const {
    DrawModelEx(sphere_, center, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, Vector3{radius, radius, radius}, color);
}

void World::DrawBoxLit(Vector3 center, Vector3 size, Color color) const {
    DrawModelEx(cube_, center, Vector3{0.0f, 1.0f, 0.0f}, 0.0f, size, color);
}

void World::DrawRange(float b) const {
    // Room: 80 m x 80 m, player at the centre, eye height 1.6 m.
    const Color floorC = Shade(Color{44, 47, 55, 255}, b);
    const Color wallC = Shade(Color{58, 62, 72, 255}, b);
    const Color sideC = Shade(Color{52, 56, 66, 255}, b);
    const Color pillarC = Shade(Color{70, 74, 86, 255}, b);
    const Color lineC = Shade(Color{75, 80, 95, 255}, b);
    const Color markC = Shade(Color{190, 60, 70, 255}, b);

    DrawBoxLit(Vector3{0.0f, -0.05f, 0.0f}, Vector3{80.0f, 0.1f, 80.0f}, floorC);
    DrawBoxLit(Vector3{0.0f, 7.0f, -40.0f}, Vector3{80.0f, 14.0f, 0.5f}, wallC);
    DrawBoxLit(Vector3{0.0f, 7.0f, 40.0f}, Vector3{80.0f, 14.0f, 0.5f}, wallC);
    DrawBoxLit(Vector3{-40.0f, 7.0f, 0.0f}, Vector3{0.5f, 14.0f, 80.0f}, sideC);
    DrawBoxLit(Vector3{40.0f, 7.0f, 0.0f}, Vector3{0.5f, 14.0f, 80.0f}, sideC);

    // Floor grid every 2 m for a sense of distance and motion.
    for (int i = -20; i <= 20; ++i) {
        const float p = static_cast<float>(i) * 2.0f;
        DrawLine3D(Vector3{p, 0.01f, -40.0f}, Vector3{p, 0.01f, 40.0f}, lineC);
        DrawLine3D(Vector3{-40.0f, 0.01f, p}, Vector3{40.0f, 0.01f, p}, lineC);
    }
    // Distance markers in front of the player: 10, 20, 30 m.
    for (int d = 10; d <= 30; d += 10) {
        const float z = -static_cast<float>(d);
        DrawBoxLit(Vector3{0.0f, 0.012f, z}, Vector3{30.0f, 0.004f, 0.08f}, markC);
    }
    // Pillars along the walls to give peripheral reference points.
    for (int i = -3; i <= 3; ++i) {
        const float p = static_cast<float>(i) * 11.0f;
        DrawBoxLit(Vector3{p, 7.0f, -39.2f}, Vector3{1.2f, 14.0f, 1.2f}, pillarC);
        DrawBoxLit(Vector3{p, 7.0f, 39.2f}, Vector3{1.2f, 14.0f, 1.2f}, pillarC);
        DrawBoxLit(Vector3{-39.2f, 7.0f, p}, Vector3{1.2f, 14.0f, 1.2f}, pillarC);
        DrawBoxLit(Vector3{39.2f, 7.0f, p}, Vector3{1.2f, 14.0f, 1.2f}, pillarC);
    }
}

void World::DrawCovers(float b) const {
    const Color c = Shade(Color{96, 88, 80, 255}, b);
    for (const Box& box : covers_) DrawBoxLit(box.center, box.size, c);
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
