module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

module Editor.context;

import Engine.engine;
import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.glm;

namespace {

constexpr float GROUND_RAY_EPSILON = 1e-4f;
constexpr float SPAWN_PICK_MAX_DISTANCE = 500.0f;
constexpr float SPAWN_GROUND_MAX_DISTANCE = 200.0f;
constexpr float SPAWN_FALLBACK_DISTANCE = 10.0f;
constexpr float MIN_FLY_RADIUS = 0.25f;
constexpr float FLY_FRAMING_MARGIN = 1.15f;
constexpr float FLY_ORTHO_SCALE = 2.3f;
constexpr float MIN_FIT_SINE = 1e-3f;
constexpr float MIN_FLIGHT_DURATION = 1e-3f;
constexpr float HALF_TURN_DEGREES = 180.0f;
constexpr float FULL_TURN_DEGREES = 360.0f;

}

std::string displayName(World& world, EntityHandle entity) {
    if (!world.isAlive(entity)) return "(none)";
    const std::string& name = world.getName(entity);
    if (!name.empty()) return name;
    return std::format("Entity {}", entity.index);
}

std::string meshLabel(Engine& engine, uint32_t mesh) {
    if (mesh == NO_MESH) return "empty";
    const std::string name = engine.meshName(mesh);
    return name.empty() ? std::format("mesh {}", mesh) : name;
}

std::optional<glm::vec3> surfacePoint(EditorContext& ctx, const Ray& ray, const RayHit& hit) {
    const glm::vec4 bounds = ctx.world.getWorldBounds(hit.entity);
    if (glm::distance(glm::vec3(bounds), ray.origin) <= bounds.w) return std::nullopt;
    const float distance = std::clamp(glm::dot(glm::vec3(bounds) - ray.origin, ray.direction), hit.distance, hit.distance + bounds.w);
    return ray.origin + ray.direction * distance;
}

SpawnPoint spawnPoint(EditorContext& ctx) {
    const glm::vec2 size = ctx.engine.windowSize();
    if (ctx.cursorValid) {
        const std::optional<glm::vec2> screen = ctx.engine.worldToScreen(ctx.cursor);
        if (screen && screen->x >= 0.0f && screen->y >= 0.0f && screen->x <= size.x && screen->y <= size.y) return {ctx.cursor, ctx.cursorOnSurface};
    }
    const glm::vec2 center = size * 0.5f;
    const Ray ray = ctx.engine.screenRay(center.x, center.y);
    if (const std::optional<RayHit> hit = ctx.engine.pick(center.x, center.y); hit && hit->distance < SPAWN_PICK_MAX_DISTANCE) {
        if (const std::optional<glm::vec3> point = surfacePoint(ctx, ray, *hit)) return {*point, true};
    }
    if (ray.direction.y < -GROUND_RAY_EPSILON) {
        const float distance = -ray.origin.y / ray.direction.y;
        if (distance > 0.0f && distance < SPAWN_GROUND_MAX_DISTANCE) return {ray.origin + ray.direction * distance, true};
    }
    const Camera& camera = ctx.world.camera();
    return {camera.getPosition() + cameraForward(camera) * SPAWN_FALLBACK_DISTANCE, false};
}

glm::vec3 cameraForward(const Camera& camera) {
    const float yaw = glm::radians(camera.getYaw());
    const float pitch = glm::radians(camera.getPitch());
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch)));
}

void flyTo(EditorContext& ctx, const glm::vec4& sphere) {
    Camera& camera = ctx.world.camera();
    const glm::vec3 forward = cameraForward(camera);
    const float radius = std::max(sphere.w, MIN_FLY_RADIUS);
    const float halfFov = glm::radians(camera.getFov()) * 0.5f;
    const glm::vec2 size = ctx.engine.windowSize();
    const float aspect = size.y > 0.0f ? size.x / size.y : 1.0f;
    const float fit = std::min(halfFov, std::atan(std::tan(halfFov) * aspect));
    const float distance = radius / std::max(std::sin(fit), MIN_FIT_SINE) * FLY_FRAMING_MARGIN;
    CameraFlight& flight = ctx.flight;
    flight.active = true;
    flight.t = 0.0f;
    flight.from = camera.getPosition();
    flight.to = glm::vec3(sphere) - forward * distance;
    flight.fromYaw = flight.toYaw = camera.getYaw();
    flight.fromPitch = flight.toPitch = camera.getPitch();
    flight.fromOrtho = camera.isOrthographic() ? camera.getOrthoHeight() : 0.0f;
    flight.toOrtho = camera.isOrthographic() ? radius * FLY_ORTHO_SCALE / std::min(1.0f, aspect) : 0.0f;
    if (distance * 2.0f > camera.getFarPlane()) camera.setFarPlane(distance * 2.0f);
}

void updateFlight(EditorContext& ctx, float dt) {
    CameraFlight& flight = ctx.flight;
    if (!flight.active) return;
    flight.t = std::min(1.0f, flight.t + dt / std::max(flight.duration, MIN_FLIGHT_DURATION));
    const float eased = 1.0f - std::pow(1.0f - flight.t, 3.0f);
    Camera& camera = ctx.world.camera();
    camera.setPos(flight.from + (flight.to - flight.from) * eased);
    const float yawDelta = std::fmod(flight.toYaw - flight.fromYaw + HALF_TURN_DEGREES + FULL_TURN_DEGREES, FULL_TURN_DEGREES) - HALF_TURN_DEGREES;
    camera.setOrientation(flight.fromYaw + yawDelta * eased, flight.fromPitch + (flight.toPitch - flight.fromPitch) * eased);
    if (flight.toOrtho > 0.0f && flight.fromOrtho > 0.0f) camera.setOrthoHeight(flight.fromOrtho + (flight.toOrtho - flight.fromOrtho) * eased);
    if (flight.t < 1.0f) return;
    flight.active = false;
    if (flight.toOrtho > 0.0f) {
        camera.setOrthographic(true);
        camera.setOrthoHeight(flight.toOrtho);
    }
    flight.fromOrtho = flight.toOrtho = 0.0f;
}
