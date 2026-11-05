module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;
import Engine.Physics;

namespace {

constexpr float kEpsilon = 1.0e-5f;
constexpr float kMinNormalY = 0.2f;

struct GroundProbe {
    World* world;
    EntityHandle body;
};

float blendFactor(float rate, float deltaTime) { return 1.0f - std::exp(-rate * deltaTime); }

glm::vec3 horizontal(const glm::vec3& vector) { return glm::vec3(vector.x, 0.0f, vector.z); }

glm::vec3 clampTilt(const glm::vec3& normal, float maxRadians) {
    const float angle = std::acos(std::clamp(normal.y, -1.0f, 1.0f));
    if (angle <= maxRadians) return normal;
    const glm::vec3 axisAway = horizontal(normal);
    if (glm::length(axisAway) < kEpsilon) return glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::normalize(glm::vec3(0.0f, std::cos(maxRadians), 0.0f) + glm::normalize(axisAway) * std::sin(maxRadians));
}

glm::vec3 steerDirection(const Walker& walker, const glm::vec3& position) {
    const glm::vec3 fromInput = walker.inputRight * walker.moveInput.x + walker.inputForward * walker.moveInput.y;
    if (glm::length(horizontal(fromInput)) > kEpsilon) return horizontal(fromInput);
    if (!walker.hasMoveTarget) return glm::vec3(0.0f);
    return horizontal(walker.moveTarget - position);
}

glm::vec3 desiredVelocity(Walker& walker, const glm::vec3& position) {
    const glm::vec3 steer = steerDirection(walker, position);
    const float distance = glm::length(steer);
    const bool fromInput = glm::length(walker.moveInput) > kEpsilon;
    walker.arrived = !fromInput && walker.hasMoveTarget && distance < walker.arrivalRadius;
    if (distance < kEpsilon || walker.arrived) return glm::vec3(0.0f);
    const float slow = fromInput ? 1.0f : std::clamp(distance / std::max(walker.slowRadius, kEpsilon), 0.15f, 1.0f);
    return steer / distance * walker.maxSpeed * slow * walker.speedScale;
}

void turnHeading(Walker& walker, float deltaTime) {
    if (walker.speed < kEpsilon) return;
    const glm::vec3 wanted = glm::normalize(horizontal(walker.velocity));
    const float angle = std::atan2(glm::dot(glm::cross(walker.heading, wanted), glm::vec3(0.0f, 1.0f, 0.0f)), glm::dot(walker.heading, wanted));
    const float step = std::clamp(angle, -walker.turnRate * deltaTime, walker.turnRate * deltaTime);
    walker.heading = glm::normalize(ikRotateAround(walker.heading, glm::vec3(0.0f, 1.0f, 0.0f), step));
}

struct PlaneFit {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float centerZ = 0.0f;
    float slopeX = 0.0f;
    float slopeZ = 0.0f;
    bool valid = false;
};

PlaneFit fitFeetPlane(const Walker& walker) {
    PlaneFit fit;
    size_t plantedCount = 0;
    for (const WalkerLeg& leg : walker.legs) plantedCount += leg.swinging ? 0 : 1;
    const bool plantedOnly = plantedCount >= 3;
    glm::vec3 center(0.0f);
    float count = 0.0f;
    for (const WalkerLeg& leg : walker.legs) {
        if (plantedOnly && leg.swinging) continue;
        center += leg.position;
        count += 1.0f;
    }
    if (count < 1.0f) return fit;
    center /= count;
    fit.centerX = center.x;
    fit.centerY = center.y;
    fit.centerZ = center.z;
    float sxx = 0.0f, sxz = 0.0f, szz = 0.0f, sxy = 0.0f, szy = 0.0f;
    for (const WalkerLeg& leg : walker.legs) {
        if (plantedOnly && leg.swinging) continue;
        const glm::vec3 delta = leg.position - center;
        sxx += delta.x * delta.x;
        sxz += delta.x * delta.z;
        szz += delta.z * delta.z;
        sxy += delta.x * delta.y;
        szy += delta.z * delta.y;
    }
    const float determinant = sxx * szz - sxz * sxz;
    fit.valid = true;
    if (std::abs(determinant) < kEpsilon) return fit;
    fit.slopeX = (sxy * szz - szy * sxz) / determinant;
    fit.slopeZ = (szy * sxx - sxy * sxz) / determinant;
    return fit;
}

glm::quat bodyRotation(const glm::vec3& heading, const glm::vec3& up) {
    glm::vec3 forward = heading - up * glm::dot(heading, up);
    if (glm::length(forward) < kEpsilon) forward = glm::vec3(0.0f, 0.0f, -1.0f);
    forward = glm::normalize(forward);
    const glm::vec3 right = glm::normalize(glm::cross(forward, up));
    return glm::quat_cast(glm::mat3(right, glm::cross(right, forward), -forward));
}

void writeBody(World& world, const Walker& walker, const glm::vec3& position, const glm::quat& rotation) {
    const glm::vec3 scale = world.getScale(walker.body);
    world.setWorldMatrix(walker.body, glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation) * glm::scale(glm::mat4(1.0f), scale));
}

}

GroundHit sampleWalkerGround(World& world, const Walker& walker, float x, float z, float referenceHeight) {
    GroundHit hit;
    const glm::vec3 origin(x, referenceHeight + walker.probeUp, z);
    if (walker.ground) {
        float height = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        if (!walker.ground(glm::vec3(x, referenceHeight, z), height, normal)) return hit;
        hit.point = glm::vec3(x, height, z);
        hit.normal = normal;
        hit.found = true;
        return hit;
    }
    const GroundProbe probe{&world, walker.body};
    const std::optional<PhysicsHit> result = physicsRaycast(world, origin, glm::vec3(0.0f, -1.0f, 0.0f), walker.probeUp + walker.probeDown, [&probe](EntityHandle entity) { return entity == probe.body || probe.world->isDescendantOf(entity, probe.body); });
    if (!result) return hit;
    hit.point = origin + glm::vec3(0.0f, -result->distance, 0.0f);
    hit.normal = result->normal;
    hit.found = true;
    return hit;
}

void updateWalkerBody(World& world, Walker& walker, float deltaTime) {
    glm::vec3 position = world.getWorldPosition(walker.body);
    const glm::vec3 wanted = desiredVelocity(walker, position);
    walker.velocity += (wanted - walker.velocity) * std::min(1.0f, walker.acceleration * deltaTime);
    walker.speed = glm::length(walker.velocity);
    turnHeading(walker, deltaTime);
    position += walker.velocity * deltaTime;
    const bool first = walker.legs.empty() || !walker.legs.front().placed;
    const PlaneFit fit = first ? PlaneFit{} : fitFeetPlane(walker);
    if (fit.valid) {
        const float maxTilt = glm::radians(walker.maxTiltDegrees);
        const glm::vec3 normal = clampTilt(glm::normalize(glm::vec3(-fit.slopeX, 1.0f, -fit.slopeZ)), maxTilt);
        const float planeHeight = fit.centerY + fit.slopeX * (position.x - fit.centerX) + fit.slopeZ * (position.z - fit.centerZ);
        const float targetHeight = planeHeight + walker.bodyHeight / std::max(normal.y, kMinNormalY);
        const float heightBlend = walker.settled ? blendFactor(walker.heightSmoothing, deltaTime) : 1.0f;
        const float tiltBlend = walker.settled ? blendFactor(walker.tiltSmoothing, deltaTime) : 1.0f;
        position.y += (targetHeight - position.y) * heightBlend;
        walker.smoothedUp = glm::normalize(walker.smoothedUp + (normal - walker.smoothedUp) * tiltBlend);
        walker.settled = true;
    }
    writeBody(world, walker, position, bodyRotation(walker.heading, walker.smoothedUp));
}
