module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

module Engine.Physics;

namespace {
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

float inverseMass(const Body& b) { return b.dynamic && !b.asleep && b.body->mass > 0.0f ? 1.0f / b.body->mass : 0.0f; }

bool layersCollide(const PhysicsSettings& settings, uint32_t a, uint32_t b) {
    for (uint32_t i = 0; i < 32; ++i) {
        if ((a & (1u << i)) && (settings.collisionMatrix[i] & b)) return true;
    }
    return false;
}

struct Contact {
    glm::vec3 normal;
    float depth;
};

std::optional<Contact> sphereSphere(const Body& a, const Body& b) {
    const glm::vec3 d = b.position - a.position;
    const float reach = a.radius + b.radius;
    const float dist2 = glm::dot(d, d);
    if (dist2 >= reach * reach) return std::nullopt;
    const float dist = std::sqrt(dist2);
    return Contact{dist > 1.0e-6f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f), reach - dist};
}

std::optional<Contact> sphereBox(const Body& sphere, const Body& box) {
    const glm::vec3 local = sphere.position - box.position;
    const glm::vec3 closest = glm::clamp(local, -box.half, box.half);
    const glm::vec3 diff = local - closest;
    const float dist2 = glm::dot(diff, diff);
    if (dist2 > 1.0e-10f) {
        if (dist2 >= sphere.radius * sphere.radius) return std::nullopt;
        const float dist = std::sqrt(dist2);
        return Contact{-diff / dist, sphere.radius - dist};
    }
    int axis = 0;
    float best = box.half.x - std::abs(local.x);
    const float py = box.half.y - std::abs(local.y);
    const float pz = box.half.z - std::abs(local.z);
    if (py < best) {
        best = py;
        axis = 1;
    }
    if (pz < best) {
        best = pz;
        axis = 2;
    }
    glm::vec3 outward(0.0f);
    outward[axis] = local[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{-outward, best + sphere.radius};
}

std::optional<Contact> boxBox(const Body& a, const Body& b) {
    const glm::vec3 d = b.position - a.position;
    const glm::vec3 overlap = a.half + b.half - glm::abs(d);
    if (overlap.x <= 0.0f || overlap.y <= 0.0f || overlap.z <= 0.0f) return std::nullopt;
    int axis = 0;
    if (overlap.y < overlap[axis]) axis = 1;
    if (overlap.z < overlap[axis]) axis = 2;
    glm::vec3 normal(0.0f);
    normal[axis] = d[axis] >= 0.0f ? 1.0f : -1.0f;
    return Contact{normal, overlap[axis]};
}

std::optional<Contact> collide(const Body& a, const Body& b) {
    if (!a.isBox && !b.isBox) return sphereSphere(a, b);
    if (!a.isBox && b.isBox) return sphereBox(a, b);
    if (a.isBox && !b.isBox) {
        auto c = sphereBox(b, a);
        if (c) c->normal = -c->normal;
        return c;
    }
    return boxBox(a, b);
}

float planeReach(const Body& b, const glm::vec3& n) {
    return b.isBox ? std::abs(n.x) * b.half.x + std::abs(n.y) * b.half.y + std::abs(n.z) * b.half.z : b.radius;
}
}

void wakeBody(World& world, EntityHandle e) {
    if (RigidBody* rb = world.get<RigidBody>(e)) {
        rb->sleeping = false;
        rb->sleepTimer = 0.0f;
    }
}

void stepPhysics(World& world, float dt, const PhysicsSettings& settings) {
    std::vector<Body> bodies;
    std::vector<std::pair<EntityHandle, PlaneCollider*>> planes;
    float maxRadius = 0.0f;

    const auto addBody = [&](EntityHandle e, bool isBox, float radius, const glm::vec3& half) {
        RigidBody* rb = world.get<RigidBody>(e);
        const bool dynamic = rb && !rb->isStatic && world.getParent(e).isNull();
        const float bound = isBox ? glm::length(half) : radius;
        bodies.push_back({e, world.getWorldPosition(e), isBox, radius, half, bound, world.getLayer(e), rb, dynamic, dynamic && rb->sleeping});
        maxRadius = std::max(maxRadius, bound);
    };
    world.view<SphereCollider>([&](EntityHandle e, SphereCollider& collider) {
        if (!world.has<BoxCollider>(e)) addBody(e, false, collider.radius, glm::vec3(0.0f));
    });
    world.view<BoxCollider>([&](EntityHandle e, BoxCollider& collider) { addBody(e, true, 0.0f, collider.halfExtents); });
    world.view<PlaneCollider>([&](EntityHandle e, PlaneCollider& plane) { planes.emplace_back(e, &plane); });

    for (Body& b : bodies) {
        if (!b.dynamic || b.asleep) continue;
        b.body->velocity += settings.gravity * b.body->gravityScale * dt;
        b.position += b.body->velocity * dt;
    }

    const float cell = std::max(maxRadius * 2.0f, 0.001f);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(bodies.size());
    const auto cellOf = [&](const glm::vec3& p) {
        return glm::ivec3(static_cast<int>(std::floor(p.x / cell)), static_cast<int>(std::floor(p.y / cell)), static_cast<int>(std::floor(p.z / cell)));
    };

    std::vector<Collision> collisions;
    for (int iteration = 0; iteration < std::max(1, settings.solverIterations); ++iteration) {
        const bool record = iteration == 0;
        grid.clear();
        for (uint32_t i = 0; i < bodies.size(); ++i) {
            const glm::ivec3 c = cellOf(bodies[i].position);
            grid[cellKey(c.x, c.y, c.z)].push_back(i);
        }

        for (uint32_t i = 0; i < bodies.size(); ++i) {
            Body& a = bodies[i];
            const glm::ivec3 c = cellOf(a.position);
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        const auto it = grid.find(cellKey(c.x + dx, c.y + dy, c.z + dz));
                        if (it == grid.end()) continue;
                        for (uint32_t j : it->second) {
                            if (j <= i) continue;
                            Body& b = bodies[j];
                            if (!layersCollide(settings, a.layer, b.layer)) continue;
                            const bool aActive = a.dynamic && !a.asleep;
                            const bool bActive = b.dynamic && !b.asleep;
                            if (!aActive && !bActive) continue;

                            const auto contact = collide(a, b);
                            if (!contact) continue;
                            const glm::vec3 n = contact->normal;
                            const float depth = contact->depth;

                            const float closing = glm::dot((b.body ? b.body->velocity : glm::vec3(0.0f)) - (a.body ? a.body->velocity : glm::vec3(0.0f)), n);
                            if (closing < -settings.wakeSpeed) {
                                if (a.asleep && bActive) {
                                    a.asleep = false;
                                    a.body->sleeping = false;
                                    a.body->sleepTimer = 0.0f;
                                }
                                if (b.asleep && aActive) {
                                    b.asleep = false;
                                    b.body->sleeping = false;
                                    b.body->sleepTimer = 0.0f;
                                }
                            }

                            const float ia = inverseMass(a);
                            const float ib = inverseMass(b);
                            const float total = ia + ib;
                            if (total == 0.0f) continue;

                            a.position -= n * (depth * ia / total);
                            b.position += n * (depth * ib / total);

                            const glm::vec3 va = a.body ? a.body->velocity : glm::vec3(0.0f);
                            const glm::vec3 vb = b.body ? b.body->velocity : glm::vec3(0.0f);
                            const float vn = glm::dot(vb - va, n);
                            if (vn < 0.0f) {
                                const float ea = a.body ? a.body->restitution : 0.0f;
                                const float eb = b.body ? b.body->restitution : 0.0f;
                                const float j2 = -(1.0f + std::min(ea, eb)) * vn / total;
                                if (a.dynamic && !a.asleep) a.body->velocity -= n * (j2 * ia);
                                if (b.dynamic && !b.asleep) b.body->velocity += n * (j2 * ib);
                            }
                            if (record) collisions.push_back({a.entity, b.entity, n, depth});
                        }
                    }
                }
            }
        }

        for (Body& b : bodies) {
            if (!b.dynamic || b.asleep) continue;
            for (const auto& [planeEntity, plane] : planes) {
                if (!layersCollide(settings, b.layer, world.getLayer(planeEntity))) continue;
                const glm::vec3 n = glm::normalize(plane->normal);
                const float depth = planeReach(b, n) - glm::dot(n, b.position - world.getWorldPosition(planeEntity));
                if (depth <= 0.0f) continue;
                b.position += n * depth;
                const float vn = glm::dot(b.body->velocity, n);
                if (vn < 0.0f) b.body->velocity -= n * ((1.0f + std::min(b.body->restitution, plane->restitution)) * vn);
                if (record) collisions.push_back({b.entity, planeEntity, -n, depth});
            }
        }
    }

    for (Body& b : bodies) {
        if (!b.dynamic) continue;
        if (b.asleep) {
            b.body->velocity = glm::vec3(0.0f);
            continue;
        }
        world.setPosition(b.entity, b.position);
        const float speed = glm::length(b.body->velocity);
        if (speed < settings.sleepVelocity) {
            b.body->sleepTimer += dt;
            if (b.body->sleepTimer >= settings.sleepTime) {
                b.body->sleeping = true;
                b.body->velocity = glm::vec3(0.0f);
            }
        } else {
            b.body->sleepTimer = 0.0f;
        }
    }
    for (const Collision& c : collisions) world.events().emit(c);
}

std::optional<PhysicsHit> physicsRaycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    const float len = glm::length(direction);
    if (len < 1.0e-8f) return std::nullopt;
    const glm::vec3 dir = direction / len;
    std::optional<PhysicsHit> best;
    const auto consider = [&](EntityHandle e, float t, const glm::vec3& normal) {
        if (t < 0.0f || t > maxDistance) return;
        if (!best || t < best->distance) best = PhysicsHit{e, t, normal};
    };

    world.view<SphereCollider>([&](EntityHandle e, SphereCollider& collider) {
        if (world.has<BoxCollider>(e)) return;
        const glm::vec3 center = world.getWorldPosition(e);
        const glm::vec3 oc = center - origin;
        const float along = glm::dot(oc, dir);
        const float perp2 = glm::dot(oc, oc) - along * along;
        const float r2 = collider.radius * collider.radius;
        if (perp2 > r2) return;
        const float half = std::sqrt(r2 - perp2);
        float t = along - half;
        if (t < 0.0f) t = along + half;
        const glm::vec3 point = origin + dir * t;
        const glm::vec3 n = point - center;
        consider(e, t, glm::length(n) > 1.0e-8f ? glm::normalize(n) : -dir);
    });

    world.view<BoxCollider>([&](EntityHandle e, BoxCollider& collider) {
        const glm::vec3 center = world.getWorldPosition(e);
        const glm::vec3 lo = center - collider.halfExtents;
        const glm::vec3 hi = center + collider.halfExtents;
        float tMin = 0.0f;
        float tMax = std::numeric_limits<float>::max();
        int hitAxis = -1;
        float hitSign = 0.0f;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dir[a]) < 1.0e-8f) {
                if (origin[a] < lo[a] || origin[a] > hi[a]) return;
                continue;
            }
            float t1 = (lo[a] - origin[a]) / dir[a];
            float t2 = (hi[a] - origin[a]) / dir[a];
            float sign = -1.0f;
            if (t1 > t2) {
                std::swap(t1, t2);
                sign = 1.0f;
            }
            if (t1 > tMin) {
                tMin = t1;
                hitAxis = a;
                hitSign = sign;
            }
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return;
        }
        glm::vec3 normal(0.0f);
        if (hitAxis >= 0) normal[hitAxis] = hitSign;
        consider(e, tMin, normal);
    });

    world.view<PlaneCollider>([&](EntityHandle e, PlaneCollider& plane) {
        const glm::vec3 n = glm::normalize(plane.normal);
        const float denom = glm::dot(n, dir);
        if (std::abs(denom) < 1.0e-8f) return;
        const float t = glm::dot(n, world.getWorldPosition(e) - origin) / denom;
        consider(e, t, denom < 0.0f ? n : -n);
    });
    return best;
}

void registerPhysicsComponents(World& world) {
    world.registerComponent<RigidBody>(
        "RigidBody",
        [](const RigidBody& r, std::ostream& out) { out << r.velocity.x << ' ' << r.velocity.y << ' ' << r.velocity.z << ' ' << r.mass << ' ' << r.gravityScale << ' ' << r.restitution << ' ' << int(r.isStatic) << ' ' << int(r.sleeping); },
        [](std::istream& in) -> std::optional<RigidBody> {
            RigidBody r;
            int isStatic;
            if (!(in >> r.velocity.x >> r.velocity.y >> r.velocity.z >> r.mass >> r.gravityScale >> r.restitution >> isStatic)) return std::nullopt;
            r.isStatic = isStatic != 0;
            int sleeping = 0;
            if (in >> sleeping) r.sleeping = sleeping != 0;
            return r;
        });
    world.registerComponent<SphereCollider>(
        "SphereCollider", [](const SphereCollider& c, std::ostream& out) { out << c.radius; },
        [](std::istream& in) -> std::optional<SphereCollider> {
            SphereCollider c;
            if (!(in >> c.radius)) return std::nullopt;
            return c;
        });
    world.registerComponent<BoxCollider>(
        "BoxCollider", [](const BoxCollider& c, std::ostream& out) { out << c.halfExtents.x << ' ' << c.halfExtents.y << ' ' << c.halfExtents.z; },
        [](std::istream& in) -> std::optional<BoxCollider> {
            BoxCollider c;
            if (!(in >> c.halfExtents.x >> c.halfExtents.y >> c.halfExtents.z)) return std::nullopt;
            return c;
        });
    world.registerComponent<PlaneCollider>(
        "PlaneCollider", [](const PlaneCollider& c, std::ostream& out) { out << c.normal.x << ' ' << c.normal.y << ' ' << c.normal.z << ' ' << c.restitution; },
        [](std::istream& in) -> std::optional<PlaneCollider> {
            PlaneCollider c;
            if (!(in >> c.normal.x >> c.normal.y >> c.normal.z >> c.restitution)) return std::nullopt;
            return c;
        });
}
