module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>
#include <vector>
#include <cstdint>

module Engine.PortalPhysics;

PortalCast portalSweep(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius, int cell, const PhysicsFilter& skip, bool collide, int maxCrossings) {
    PortalCast out;
    glm::vec3 o = origin;
    glm::vec3 d = glm::normalize(direction);
    float remaining = maxDistance;
    int current = cell >= 0 ? cell : portals.cellAt(origin);
    out.direction = d;
    for (int i = 0; i <= maxCrossings; ++i) {
        std::optional<PhysicsHit> hit;
        if (collide) hit = radius > 0.0f ? physicsSweepSphere(world, o, d, remaining, radius, skip) : physicsRaycast(world, o, d, remaining, skip);
        const float reach = hit ? hit->distance : remaining;
        const auto cross = portals.firstCrossing(current, o, d, reach);
        if (!cross) {
            out.hit = hit;
            out.distance += reach;
            if (out.hit) out.hit->distance = out.distance;
            out.point = o + d * reach;
            out.direction = d;
            out.cell = current;
            return out;
        }
        const Portal& p = portals.portals()[static_cast<size_t>(cross->first)];
        const glm::vec3 at = o + d * cross->second;
        d = glm::normalize(glm::vec3(p.transform * glm::vec4(d, 0.0f)));
        o = glm::vec3(p.transform * glm::vec4(at, 1.0f)) + d * 1.0e-3f;
        out.distance += cross->second + 1.0e-3f;
        remaining -= cross->second + 1.0e-3f;
        current = portals.portals()[static_cast<size_t>(p.partner)].cell;
        ++out.crossings;
        if (remaining <= 0.0f) break;
    }
    out.point = o;
    out.direction = d;
    out.cell = current;
    return out;
}

PortalCast portalRaycast(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell, const PhysicsFilter& skip) {
    const PhysicsFilter filter = [&](EntityHandle e) { return portals.isGhost(e) || (skip && skip(e)); };
    return portalSweep(portals, world, origin, direction, maxDistance, 0.0f, cell, filter);
}

std::vector<PortalOverlap> portalOverlap(const PortalSystem& portals, World& world, const glm::vec3& center, float radius, int cell, const PhysicsFilter& skip) {
    std::vector<PortalOverlap> out;
    std::unordered_set<uint64_t> seen;
    const auto key = [](EntityHandle e) { return (static_cast<uint64_t>(e.generation) << 32) | e.index; };
    const PhysicsFilter filter = [&](EntityHandle e) { return portals.isGhost(e) || (skip && skip(e)); };
    for (const PhysicsContact& c : physicsOverlapCapsule(world, center, center, radius, filter)) {
        if (seen.insert(key(c.entity)).second) out.push_back({c.entity, 0});
    }
    const int current = cell >= 0 ? cell : portals.cellAt(center);
    for (int id : portals.portalsOf(current)) {
        const Portal& p = portals.portals()[static_cast<size_t>(id)];
        const glm::vec3 q = center - p.center();
        const float side = glm::dot(q, p.normal());
        if (side < -radius || side > radius) continue;
        const glm::vec3 mapped = glm::vec3(p.transform * glm::vec4(center, 1.0f));
        for (const PhysicsContact& c : physicsOverlapCapsule(world, mapped, mapped, radius, filter)) {
            if (seen.insert(key(c.entity)).second) out.push_back({c.entity, 1});
        }
    }
    return out;
}

void installPortalPhysics(PortalSystem& portals) {
    portals.setCrossHook([](World& world, EntityHandle e, const glm::mat4& transform) {
        if (RigidBody* rb = world.get<RigidBody>(e)) rb->velocity = glm::mat3(transform) * rb->velocity;
    });
}

void stepPortalPhysics(PortalSystem& portals, World& world, float dt, const PhysicsSettings& settings) {
    stepPhysics(world, dt, settings);
    portals.updateObjects(world);
    std::vector<EntityHandle> twinned;
    world.view<RigidBody>([&](EntityHandle e, RigidBody& rb) {
        if (rb.isStatic) return;
        const EntityHandle ghost = portals.ghostOf(e);
        if (world.isAlive(ghost) && world.isVisible(ghost)) twinned.push_back(e);
    });
    for (EntityHandle e : twinned) {
        const EntityHandle ghost = portals.ghostOf(e);
        float radius = 0.0f;
        if (const BoxCollider* b = world.get<BoxCollider>(e)) radius = std::min({b->halfExtents.x, b->halfExtents.y, b->halfExtents.z});
        else if (const SphereCollider* s = world.get<SphereCollider>(e)) radius = s->radius;
        else continue;
        const glm::mat4 toGhost = world.getWorldMatrix(ghost) * glm::inverse(world.getWorldMatrix(e));
        const glm::mat3 back = glm::transpose(glm::mat3(toGhost));
        const glm::vec3 center = glm::vec3(world.getWorldMatrix(ghost)[3]);
        RigidBody* rb = world.get<RigidBody>(e);
        const PhysicsFilter filter = [&](EntityHandle o) {
            if (o == e || o == ghost) return true;
            const RigidBody* other = world.get<RigidBody>(o);
            return other && !other->isStatic;
        };
        for (const PhysicsContact& c : physicsOverlapCapsule(world, center, center, radius, filter)) {
            const glm::vec3 n = back * c.normal;
            world.setPosition(e, world.getWorldPosition(e) + n * c.depth);
            const float vn = glm::dot(rb->velocity, n);
            if (vn < 0.0f) rb->velocity -= n * ((1.0f + rb->restitution) * vn);
            wakeBody(world, e);
        }
        world.setWorldMatrix(ghost, toGhost * world.getWorldMatrix(e));
    }
}
