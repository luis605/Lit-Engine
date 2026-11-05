module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

module Engine.PortalPhysics;

namespace {
constexpr float kCrossingNudge = 1.0e-3f;
constexpr float kMinDirectionLengthSquared = 1.0e-12f;
constexpr int kMaxSweepCrossings = 256;

uint64_t entityKey(EntityHandle entity) { return (static_cast<uint64_t>(entity.generation) << 32) | entity.index; }

glm::vec3 transformPoint(const glm::mat4& matrix, const glm::vec3& point) { return glm::vec3(matrix * glm::vec4(point, 1.0f)); }
}

PortalCast portalSweep(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius, int cell, const PhysicsFilter& skip, bool collide, int maxCrossings) {
    PortalCast result;
    int current = cell >= 0 ? cell : portals.cellAt(origin);
    result.cell = current;
    if (glm::dot(direction, direction) < kMinDirectionLengthSquared) {
        result.point = origin;
        return result;
    }
    glm::vec3 rayOrigin = origin;
    glm::vec3 rayDirection = glm::normalize(direction);
    float remaining = maxDistance;
    result.direction = rayDirection;
    const int crossingLimit = std::min(maxCrossings, kMaxSweepCrossings);
    for (int attempt = 0; attempt <= crossingLimit; ++attempt) {
        std::optional<PhysicsHit> hit;
        if (collide) hit = radius > 0.0f ? physicsSweepSphere(world, rayOrigin, rayDirection, remaining, radius, skip) : physicsRaycast(world, rayOrigin, rayDirection, remaining, skip);
        const float reach = hit ? hit->distance : remaining;
        const auto crossingPoint = portals.firstCrossing(current, rayOrigin, rayDirection, reach);
        if (!crossingPoint) {
            result.hit = hit;
            result.distance += reach;
            if (result.hit) result.hit->distance = result.distance;
            result.point = rayOrigin + rayDirection * reach;
            result.direction = rayDirection;
            result.cell = current;
            return result;
        }
        const Portal& portal = portals.portals()[static_cast<size_t>(crossingPoint->first)];
        const glm::vec3 entryPoint = rayOrigin + rayDirection * crossingPoint->second;
        rayDirection = glm::normalize(glm::vec3(portal.transform * glm::vec4(rayDirection, 0.0f)));
        rayOrigin = transformPoint(portal.transform, entryPoint) + rayDirection * kCrossingNudge;
        result.distance += crossingPoint->second + kCrossingNudge;
        remaining -= crossingPoint->second + kCrossingNudge;
        current = portals.portals()[static_cast<size_t>(portal.partner)].cell;
        ++result.crossings;
        if (remaining <= 0.0f) break;
    }
    result.point = rayOrigin;
    result.direction = rayDirection;
    result.cell = current;
    return result;
}

PortalCast portalRaycast(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell, const PhysicsFilter& skip) {
    const PhysicsFilter filter = [&](EntityHandle entity) { return portals.isGhost(entity) || (skip && skip(entity)); };
    return portalSweep(portals, world, origin, direction, maxDistance, 0.0f, cell, filter);
}

std::vector<PortalOverlap> portalOverlap(const PortalSystem& portals, World& world, const glm::vec3& center, float radius, int cell, const PhysicsFilter& skip) {
    std::vector<PortalOverlap> overlaps;
    std::unordered_set<uint64_t> seen;
    const PhysicsFilter filter = [&](EntityHandle entity) { return portals.isGhost(entity) || (skip && skip(entity)); };
    for (const PhysicsContact& contact : physicsOverlapCapsule(world, center, center, radius, filter)) {
        if (seen.insert(entityKey(contact.entity)).second) overlaps.push_back({contact.entity, 0});
    }
    const int current = cell >= 0 ? cell : portals.cellAt(center);
    for (int portalId : portals.portalsOf(current)) {
        const Portal& portal = portals.portals()[static_cast<size_t>(portalId)];
        const float side = glm::dot(center - portal.center(), portal.normal());
        if (side < -radius || side > radius) continue;
        const glm::vec3 mapped = transformPoint(portal.transform, center);
        for (const PhysicsContact& contact : physicsOverlapCapsule(world, mapped, mapped, radius, filter)) {
            if (seen.insert(entityKey(contact.entity)).second) overlaps.push_back({contact.entity, 1});
        }
    }
    return overlaps;
}

void installPortalPhysics(PortalSystem& portals) {
    portals.setCrossHook([](World& world, EntityHandle entity, const glm::mat4& transform) {
        if (RigidBody* body = world.get<RigidBody>(entity)) body->velocity = glm::mat3(transform) * body->velocity;
    });
}

namespace {
float colliderRadius(World& world, EntityHandle entity) {
    if (const BoxCollider* box = world.get<BoxCollider>(entity)) return std::min({box->halfExtents.x, box->halfExtents.y, box->halfExtents.z});
    if (const SphereCollider* sphere = world.get<SphereCollider>(entity)) return sphere->radius;
    return -1.0f;
}

void pushOutOfGhostContacts(World& world, EntityHandle entity, EntityHandle ghost, float radius) {
    const glm::mat4 toGhost = world.getWorldMatrix(ghost) * glm::inverse(world.getWorldMatrix(entity));
    const glm::mat3 back = glm::transpose(glm::mat3(toGhost));
    const glm::vec3 center = glm::vec3(world.getWorldMatrix(ghost)[3]);
    RigidBody* body = world.get<RigidBody>(entity);
    const PhysicsFilter filter = [&](EntityHandle other) {
        if (other == entity || other == ghost) return true;
        const RigidBody* otherBody = world.get<RigidBody>(other);
        return otherBody && !otherBody->isStatic;
    };
    for (const PhysicsContact& contact : physicsOverlapCapsule(world, center, center, radius, filter)) {
        const glm::vec3 normal = back * contact.normal;
        world.setPosition(entity, world.getWorldPosition(entity) + normal * contact.depth);
        const float normalSpeed = glm::dot(body->velocity, normal);
        if (normalSpeed < 0.0f) body->velocity -= normal * ((1.0f + body->restitution) * normalSpeed);
        wakeBody(world, entity);
    }
    world.setWorldMatrix(ghost, toGhost * world.getWorldMatrix(entity));
}
}

void stepPortalPhysics(PortalSystem& portals, World& world, float dt, const PhysicsSettings& settings) {
    stepPhysics(world, dt, settings);
    portals.updateObjects(world);
    std::vector<EntityHandle> twinned;
    world.view<RigidBody>([&](EntityHandle entity, RigidBody& body) {
        if (body.isStatic) return;
        const EntityHandle ghost = portals.ghostOf(entity);
        if (world.isAlive(ghost) && world.isVisible(ghost)) twinned.push_back(entity);
    });
    for (EntityHandle entity : twinned) {
        const float radius = colliderRadius(world, entity);
        if (radius < 0.0f) continue;
        pushOutOfGhostContacts(world, entity, portals.ghostOf(entity), radius);
    }
}
