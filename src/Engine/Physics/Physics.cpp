module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <utility>
#include <vector>

module Engine.Physics;

namespace {
constexpr float kSphereMinDistance = 1.0e-6f;
constexpr float kBoxMinDistanceSquared = 1.0e-10f;
constexpr uint32_t kLayerCount = 32;

struct Body {
    EntityHandle entity;
    glm::vec3 position;
    bool isBox;
    float radius;
    glm::vec3 half;
    float boundRadius;
    uint32_t layer;
    RigidBody* body;
    bool dynamic;
    bool asleep;
};

uint64_t cellKey(int x, int y, int z) {
    constexpr uint64_t mask = 0x1FFFFF;
    return ((static_cast<uint64_t>(x) & mask) << 42) | ((static_cast<uint64_t>(y) & mask) << 21) | (static_cast<uint64_t>(z) & mask);
}

float inverseMass(const Body& body) { return body.dynamic && !body.asleep && body.body->mass > 0.0f ? 1.0f / body.body->mass : 0.0f; }

bool layersCollide(const PhysicsSettings& settings, uint32_t layerA, uint32_t layerB) {
    for (uint32_t layer = 0; layer < kLayerCount; ++layer) {
        if ((layerA & (1u << layer)) && (settings.collisionMatrix[layer] & layerB)) return true;
    }
    return false;
}

struct Contact {
    glm::vec3 normal;
    float depth;
};

std::optional<Contact> sphereSphere(const Body& first, const Body& second) {
    const glm::vec3 offset = second.position - first.position;
    const float reach = first.radius + second.radius;
    const float distanceSquared = glm::dot(offset, offset);
    if (distanceSquared >= reach * reach) return std::nullopt;
    const float distance = std::sqrt(distanceSquared);
    return Contact{distance > kSphereMinDistance ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f), reach - distance};
}

std::optional<Contact> sphereBox(const Body& sphere, const Body& box) {
    const glm::vec3 local = sphere.position - box.position;
    const glm::vec3 closest = glm::clamp(local, -box.half, box.half);
    const glm::vec3 difference = local - closest;
    const float distanceSquared = glm::dot(difference, difference);
    if (distanceSquared > kBoxMinDistanceSquared) {
        if (distanceSquared >= sphere.radius * sphere.radius) return std::nullopt;
        const float distance = std::sqrt(distanceSquared);
        return Contact{-difference / distance, sphere.radius - distance};
    }
    int axis = 0;
    float shallowest = box.half.x - std::abs(local.x);
    const float penetrationY = box.half.y - std::abs(local.y);
    const float penetrationZ = box.half.z - std::abs(local.z);
    if (penetrationY < shallowest) {
        shallowest = penetrationY;
        axis = 1;
    }
    if (penetrationZ < shallowest) {
        shallowest = penetrationZ;
        axis = 2;
    }
    glm::vec3 outward(0.0f);
    outward[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{-outward, shallowest + sphere.radius};
}

std::optional<Contact> boxBox(const Body& first, const Body& second) {
    const glm::vec3 offset = second.position - first.position;
    const glm::vec3 overlap = first.half + second.half - glm::abs(offset);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return std::nullopt;
    int axis = 0;
    if (overlap.y < overlap[axis]) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    glm::vec3 normal(0.0f);
    normal[axis] = offset[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{normal, overlap[axis]};
}

std::optional<Contact> collide(const Body& first, const Body& second) {
    if (!first.isBox && !second.isBox) return sphereSphere(first, second);
    if (!first.isBox && second.isBox) return sphereBox(first, second);
    if (first.isBox && !second.isBox) {
        auto contact = sphereBox(second, first);
        if (contact) contact->normal = -contact->normal;
        return contact;
    }
    return boxBox(first, second);
}

float planeReach(const Body& body, const glm::vec3& normal) {
    return body.isBox ? std::abs(normal.x) * body.half.x + std::abs(normal.y) * body.half.y + std::abs(normal.z) * body.half.z : body.radius;
}
}

void wakeBody(World& world, EntityHandle entity) {
    if (RigidBody* body = world.get<RigidBody>(entity)) {
        body->sleeping = false;
        body->sleepTimer = 0.0f;
    }
}

namespace {
constexpr float kMinCellSize = 0.001f;
constexpr int kNeighborRadius = 1;

using PlaneEntry = std::pair<EntityHandle, PlaneCollider*>;

void collectBodies(World& world, std::vector<Body>& bodies, std::vector<PlaneEntry>& planes, float& maxRadius) {
    const auto addBody = [&](EntityHandle entity, bool isBox, float radius, const glm::vec3& half) {
        RigidBody* rigidBody = world.get<RigidBody>(entity);
        const bool dynamic = rigidBody && !rigidBody->isStatic && world.getParent(entity).isNull();
        const float bound = isBox ? glm::length(half) : radius;
        bodies.push_back({entity, world.getWorldPosition(entity), isBox, radius, half, bound, world.getLayer(entity), rigidBody, dynamic, dynamic && rigidBody->sleeping});
        maxRadius = std::max(maxRadius, bound);
    };
    world.view<SphereCollider>([&](EntityHandle entity, SphereCollider& collider) {
        if (!world.has<BoxCollider>(entity)) addBody(entity, false, collider.radius, glm::vec3(0.0f));
    });
    world.view<BoxCollider>([&](EntityHandle entity, BoxCollider& collider) { addBody(entity, true, 0.0f, collider.halfExtents); });
    world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) { planes.emplace_back(entity, &plane); });
}

void wakeIfStruck(Body& body, bool otherActive) {
    if (!body.asleep || !otherActive) return;
    body.asleep = false;
    body.body->sleeping = false;
    body.body->sleepTimer = 0.0f;
}

void resolvePair(Body& first, Body& second, const PhysicsSettings& settings, bool record, std::vector<Collision>& collisions) {
    if (!layersCollide(settings, first.layer, second.layer)) return;
    const bool firstActive = first.dynamic && !first.asleep;
    const bool secondActive = second.dynamic && !second.asleep;
    if (!firstActive && !secondActive) return;

    const auto contact = collide(first, second);
    if (!contact) return;
    const glm::vec3 normal = contact->normal;
    const float depth = contact->depth;

    const glm::vec3 firstVelocity = first.body ? first.body->velocity : glm::vec3(0.0f);
    const glm::vec3 secondVelocity = second.body ? second.body->velocity : glm::vec3(0.0f);
    const float closing = glm::dot(secondVelocity - firstVelocity, normal);
    if (closing < -settings.wakeSpeed) {
        wakeIfStruck(first, secondActive);
        wakeIfStruck(second, firstActive);
    }

    const float firstInverseMass = inverseMass(first);
    const float secondInverseMass = inverseMass(second);
    const float totalInverseMass = firstInverseMass + secondInverseMass;
    if (totalInverseMass == 0.0f) return;

    first.position -= normal * (depth * firstInverseMass / totalInverseMass);
    second.position += normal * (depth * secondInverseMass / totalInverseMass);

    const glm::vec3 currentFirstVelocity = first.body ? first.body->velocity : glm::vec3(0.0f);
    const glm::vec3 currentSecondVelocity = second.body ? second.body->velocity : glm::vec3(0.0f);
    const float normalSpeed = glm::dot(currentSecondVelocity - currentFirstVelocity, normal);
    if (normalSpeed < 0.0f) {
        const float firstRestitution = first.body ? first.body->restitution : 0.0f;
        const float secondRestitution = second.body ? second.body->restitution : 0.0f;
        const float impulse = -(1.0f + std::min(firstRestitution, secondRestitution)) * normalSpeed / totalInverseMass;
        if (first.dynamic && !first.asleep) first.body->velocity -= normal * (impulse * firstInverseMass);
        if (second.dynamic && !second.asleep) second.body->velocity += normal * (impulse * secondInverseMass);
    }
    if (record) collisions.push_back({first.entity, second.entity, normal, depth, std::max(0.0f, -closing)});
}

void resolvePlanes(World& world, std::vector<Body>& bodies, const std::vector<PlaneEntry>& planes, const PhysicsSettings& settings, bool record, std::vector<Collision>& collisions) {
    for (Body& body : bodies) {
        if (!body.dynamic || body.asleep) continue;
        for (const auto& [planeEntity, plane] : planes) {
            if (!layersCollide(settings, body.layer, world.getLayer(planeEntity))) continue;
            if (glm::length(plane->normal) < 1.0e-8f) continue;
            const glm::vec3 normal = glm::normalize(plane->normal);
            const float closingSpeed = std::max(0.0f, -glm::dot(body.body->velocity, normal));
            const float depth = planeReach(body, normal) - glm::dot(normal, body.position - world.getWorldPosition(planeEntity));
            if (depth <= 0.0f) continue;
            body.position += normal * depth;
            const float normalSpeed = glm::dot(body.body->velocity, normal);
            if (normalSpeed < 0.0f) body.body->velocity -= normal * ((1.0f + std::min(body.body->restitution, plane->restitution)) * normalSpeed);
            if (record) collisions.push_back({body.entity, planeEntity, -normal, depth, closingSpeed});
        }
    }
}

void commitBodies(World& world, std::vector<Body>& bodies, float dt, const PhysicsSettings& settings) {
    for (Body& body : bodies) {
        if (!body.dynamic) continue;
        if (body.asleep) {
            body.body->velocity = glm::vec3(0.0f);
            continue;
        }
        world.setPosition(body.entity, body.position);
        if (glm::length(body.body->velocity) >= settings.sleepVelocity) {
            body.body->sleepTimer = 0.0f;
            continue;
        }
        body.body->sleepTimer += dt;
        if (body.body->sleepTimer >= settings.sleepTime) {
            body.body->sleeping = true;
            body.body->velocity = glm::vec3(0.0f);
        }
    }
}
}

void stepPhysics(World& world, float dt, const PhysicsSettings& settings) {
    std::vector<Body> bodies;
    std::vector<PlaneEntry> planes;
    float maxRadius = 0.0f;
    collectBodies(world, bodies, planes, maxRadius);

    for (Body& body : bodies) {
        if (!body.dynamic || body.asleep) continue;
        body.body->velocity += settings.gravity * body.body->gravityScale * dt;
        body.position += body.body->velocity * dt;
    }

    const float cellSize = std::max(maxRadius * 2.0f, kMinCellSize);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(bodies.size());
    const auto cellOf = [&](const glm::vec3& position) {
        return glm::ivec3(static_cast<int>(std::floor(position.x / cellSize)), static_cast<int>(std::floor(position.y / cellSize)), static_cast<int>(std::floor(position.z / cellSize)));
    };

    std::vector<Collision> collisions;
    const int iterations = std::max(1, settings.solverIterations);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const bool record = iteration == 0;
        grid.clear();
        for (uint32_t index = 0; index < bodies.size(); ++index) {
            const glm::ivec3 cell = cellOf(bodies[index].position);
            grid[cellKey(cell.x, cell.y, cell.z)].push_back(index);
        }
        for (uint32_t index = 0; index < bodies.size(); ++index) {
            const glm::ivec3 cell = cellOf(bodies[index].position);
            for (int dx = -kNeighborRadius; dx <= kNeighborRadius; ++dx) {
                for (int dy = -kNeighborRadius; dy <= kNeighborRadius; ++dy) {
                    for (int dz = -kNeighborRadius; dz <= kNeighborRadius; ++dz) {
                        const auto neighbors = grid.find(cellKey(cell.x + dx, cell.y + dy, cell.z + dz));
                        if (neighbors == grid.end()) continue;
                        for (uint32_t other : neighbors->second) {
                            if (other > index) resolvePair(bodies[index], bodies[other], settings, record, collisions);
                        }
                    }
                }
            }
        }
        resolvePlanes(world, bodies, planes, settings, record, collisions);
    }

    commitBodies(world, bodies, dt, settings);
    for (const Collision& collision : collisions) world.events().emit(collision);
}

namespace {
constexpr float kMinCastDirection = 1.0e-8f;
constexpr float kMinNormalLength = 1.0e-8f;
constexpr float kMinSegmentLengthSquared = 1.0e-12f;
constexpr float kMinCapsuleDistanceSquared = 1.0e-10f;
constexpr float kMinCapsuleDistance = 1.0e-6f;
constexpr int kCapsuleRefineIterations = 4;

using CastCandidate = std::function<void(EntityHandle, float, const glm::vec3&)>;

void castSpheres(World& world, const glm::vec3& origin, const glm::vec3& direction, float inflate, const CastCandidate& consider) {
    world.view<SphereCollider>([&](EntityHandle entity, SphereCollider& collider) {
        if (world.has<BoxCollider>(entity)) return;
        const glm::vec3 center = world.getWorldPosition(entity);
        const glm::vec3 toCenter = center - origin;
        const float along = glm::dot(toCenter, direction);
        const float perpendicularSquared = glm::dot(toCenter, toCenter) - along * along;
        const float reach = collider.radius + inflate;
        const float reachSquared = reach * reach;
        if (perpendicularSquared > reachSquared) return;
        const float halfChord = std::sqrt(reachSquared - perpendicularSquared);
        float distance = along - halfChord;
        if (distance < 0.0f) distance = along + halfChord;
        const glm::vec3 offset = origin + direction * distance - center;
        consider(entity, distance, glm::length(offset) > kMinCastDirection ? glm::normalize(offset) : -direction);
    });
}

void castBoxes(World& world, const glm::vec3& origin, const glm::vec3& direction, float inflate, const CastCandidate& consider) {
    world.view<BoxCollider>([&](EntityHandle entity, BoxCollider& collider) {
        const glm::vec3 center = world.getWorldPosition(entity);
        const glm::vec3 low = center - collider.halfExtents - inflate;
        const glm::vec3 high = center + collider.halfExtents + inflate;
        float enter = 0.0f;
        float leave = std::numeric_limits<float>::max();
        int hitAxis = -1;
        float hitSign = 0.0f;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(direction[axis]) < kMinCastDirection) {
                if (origin[axis] < low[axis] || origin[axis] > high[axis]) return;
                continue;
            }
            float nearT = (low[axis] - origin[axis]) / direction[axis];
            float farT = (high[axis] - origin[axis]) / direction[axis];
            float sign = -1.0f;
            if (nearT > farT) {
                std::swap(nearT, farT);
                sign = 1.0f;
            }
            if (nearT > enter) {
                enter = nearT;
                hitAxis = axis;
                hitSign = sign;
            }
            leave = std::min(leave, farT);
            if (enter > leave) return;
        }
        glm::vec3 normal(0.0f);
        if (hitAxis >= 0) normal[hitAxis] = hitSign;
        consider(entity, enter, normal);
    });
}

void castPlanes(World& world, const glm::vec3& origin, const glm::vec3& direction, float inflate, const CastCandidate& consider) {
    world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) {
        if (glm::length(plane.normal) < kMinNormalLength) return;
        const glm::vec3 normal = glm::normalize(plane.normal);
        const float denominator = glm::dot(normal, direction);
        if (std::abs(denominator) < kMinCastDirection) return;
        const glm::vec3 planePoint = world.getWorldPosition(entity);
        const float side = glm::dot(normal, origin - planePoint) >= 0.0f ? 1.0f : -1.0f;
        const float distance = (glm::dot(normal, planePoint - origin) + side * inflate) / denominator;
        consider(entity, distance, denominator < 0.0f ? normal : -normal);
    });
}

std::optional<PhysicsHit> castImpl(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float inflate, const PhysicsFilter& skip) {
    const float length = glm::length(direction);
    if (length < kMinCastDirection) return std::nullopt;
    const glm::vec3 unitDirection = direction / length;
    std::optional<PhysicsHit> best;
    const CastCandidate consider = [&](EntityHandle entity, float distance, const glm::vec3& normal) {
        if ((skip && skip(entity)) || distance < 0.0f || distance > maxDistance) return;
        if (!best || distance < best->distance) best = PhysicsHit{entity, distance, normal};
    };
    castSpheres(world, origin, unitDirection, inflate, consider);
    castBoxes(world, origin, unitDirection, inflate, consider);
    castPlanes(world, origin, unitDirection, inflate, consider);
    return best;
}

glm::vec3 closestOnSegment(const glm::vec3& start, const glm::vec3& end, const glm::vec3& point) {
    const glm::vec3 segment = end - start;
    const float lengthSquared = glm::dot(segment, segment);
    if (lengthSquared < kMinSegmentLengthSquared) return start;
    return start + segment * glm::clamp(glm::dot(point - start, segment) / lengthSquared, 0.0f, 1.0f);
}

using ContactSink = std::function<void(EntityHandle, const glm::vec3&, float)>;

void capsuleVersusBox(const glm::vec3& capsuleStart, const glm::vec3& capsuleEnd, float radius, EntityHandle entity, const glm::vec3& center, const glm::vec3& halfExtents, const ContactSink& add) {
    const glm::vec3 low = center - halfExtents;
    const glm::vec3 high = center + halfExtents;
    glm::vec3 onSegment = closestOnSegment(capsuleStart, capsuleEnd, center);
    glm::vec3 onBox = glm::clamp(onSegment, low, high);
    for (int iteration = 0; iteration < kCapsuleRefineIterations; ++iteration) {
        onSegment = closestOnSegment(capsuleStart, capsuleEnd, onBox);
        onBox = glm::clamp(onSegment, low, high);
    }
    const glm::vec3 separation = onSegment - onBox;
    const float distanceSquared = glm::dot(separation, separation);
    if (distanceSquared > kMinCapsuleDistanceSquared) {
        if (distanceSquared >= radius * radius) return;
        const float distance = std::sqrt(distanceSquared);
        add(entity, separation / distance, radius - distance);
        return;
    }
    const glm::vec3 local = onSegment - center;
    int axis = 0;
    float shallowest = halfExtents.x - std::abs(local.x);
    for (int candidate = 1; candidate < 3; ++candidate) {
        const float penetration = halfExtents[candidate] - std::abs(local[candidate]);
        if (penetration < shallowest) {
            shallowest = penetration;
            axis = candidate;
        }
    }
    glm::vec3 outward(0.0f);
    outward[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    add(entity, outward, shallowest + radius);
}
}

std::optional<PhysicsHit> physicsRaycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip) {
    return castImpl(world, origin, direction, maxDistance, 0.0f, skip);
}

std::optional<PhysicsHit> physicsSweepSphere(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius, const PhysicsFilter& skip) {
    return castImpl(world, origin, direction, maxDistance, radius, skip);
}

std::vector<PhysicsContact> physicsOverlapCapsule(World& world, const glm::vec3& capsuleStart, const glm::vec3& capsuleEnd, float radius, const PhysicsFilter& skip) {
    std::vector<PhysicsContact> contacts;
    const ContactSink add = [&](EntityHandle entity, const glm::vec3& normal, float depth) {
        if (depth > 0.0f) contacts.push_back({entity, normal, depth});
    };
    world.view<SphereCollider>([&](EntityHandle entity, SphereCollider& collider) {
        if (world.has<BoxCollider>(entity) || (skip && skip(entity))) return;
        const glm::vec3 center = world.getWorldPosition(entity);
        const glm::vec3 offset = closestOnSegment(capsuleStart, capsuleEnd, center) - center;
        const float reach = radius + collider.radius;
        const float distance = glm::length(offset);
        if (distance >= reach) return;
        add(entity, distance > kMinCapsuleDistance ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f), reach - distance);
    });
    world.view<BoxCollider>([&](EntityHandle entity, BoxCollider& collider) {
        if (skip && skip(entity)) return;
        capsuleVersusBox(capsuleStart, capsuleEnd, radius, entity, world.getWorldPosition(entity), collider.halfExtents, add);
    });
    world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) {
        if ((skip && skip(entity)) || glm::length(plane.normal) < kMinNormalLength) return;
        const glm::vec3 normal = glm::normalize(plane.normal);
        const glm::vec3 planePoint = world.getWorldPosition(entity);
        add(entity, normal, radius - std::min(glm::dot(normal, capsuleStart - planePoint), glm::dot(normal, capsuleEnd - planePoint)));
    });
    return contacts;
}

void registerPhysicsComponents(World& world) {
    world.registerComponent<RigidBody>(
        "RigidBody",
        [](const RigidBody& body, std::ostream& output) { output << body.velocity.x << ' ' << body.velocity.y << ' ' << body.velocity.z << ' ' << body.mass << ' ' << body.gravityScale << ' ' << body.restitution << ' ' << int(body.isStatic) << ' ' << int(body.sleeping); },
        [](std::istream& input) -> std::optional<RigidBody> {
            RigidBody body;
            int isStatic = 0;
            if (!(input >> body.velocity.x >> body.velocity.y >> body.velocity.z >> body.mass >> body.gravityScale >> body.restitution >> isStatic)) return std::nullopt;
            body.isStatic = isStatic != 0;
            int sleeping = 0;
            if (input >> sleeping) body.sleeping = sleeping != 0;
            return body;
        });
    world.registerComponent<SphereCollider>(
        "SphereCollider", [](const SphereCollider& collider, std::ostream& output) { output << collider.radius; },
        [](std::istream& input) -> std::optional<SphereCollider> {
            SphereCollider collider;
            if (!(input >> collider.radius)) return std::nullopt;
            return collider;
        });
    world.registerComponent<BoxCollider>(
        "BoxCollider", [](const BoxCollider& collider, std::ostream& output) { output << collider.halfExtents.x << ' ' << collider.halfExtents.y << ' ' << collider.halfExtents.z; },
        [](std::istream& input) -> std::optional<BoxCollider> {
            BoxCollider collider;
            if (!(input >> collider.halfExtents.x >> collider.halfExtents.y >> collider.halfExtents.z)) return std::nullopt;
            return collider;
        });
    world.registerComponent<PlaneCollider>(
        "PlaneCollider", [](const PlaneCollider& collider, std::ostream& output) { output << collider.normal.x << ' ' << collider.normal.y << ' ' << collider.normal.z << ' ' << collider.restitution; },
        [](std::istream& input) -> std::optional<PlaneCollider> {
            PlaneCollider collider;
            if (!(input >> collider.normal.x >> collider.normal.y >> collider.normal.z >> collider.restitution)) return std::nullopt;
            return collider;
        });
}
