module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <istream>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <vector>

module Engine.Physics;

namespace {
struct Body {
    EntityHandle entity;
    glm::vec3 position;
    float radius;
    RigidBody* body;
    bool dynamic;
};

uint64_t cellKey(int x, int y, int z) {
    constexpr uint64_t mask = 0x1FFFFF;
    return ((static_cast<uint64_t>(x) & mask) << 42) | ((static_cast<uint64_t>(y) & mask) << 21) | (static_cast<uint64_t>(z) & mask);
}

float inverseMass(const Body& b) { return b.dynamic && b.body->mass > 0.0f ? 1.0f / b.body->mass : 0.0f; }
}

void stepPhysics(World& world, float dt, const PhysicsSettings& settings) {
    std::vector<Body> bodies;
    std::vector<std::pair<EntityHandle, PlaneCollider*>> planes;
    float maxRadius = 0.0f;

    world.view<SphereCollider>([&](EntityHandle e, SphereCollider& collider) {
        RigidBody* rb = world.get<RigidBody>(e);
        const bool dynamic = rb && !rb->isStatic && world.getParent(e).isNull();
        bodies.push_back({e, world.getWorldPosition(e), collider.radius, rb, dynamic});
        maxRadius = std::max(maxRadius, collider.radius);
    });
    world.view<PlaneCollider>([&](EntityHandle e, PlaneCollider& plane) { planes.emplace_back(e, &plane); });

    for (Body& b : bodies) {
        if (!b.dynamic) continue;
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
                            const float ia = inverseMass(a);
                            const float ib = inverseMass(b);
                            if (ia + ib == 0.0f) continue;

                            const glm::vec3 d = b.position - a.position;
                            const float reach = a.radius + b.radius;
                            const float dist2 = glm::dot(d, d);
                            if (dist2 >= reach * reach) continue;
                            const float dist = std::sqrt(dist2);
                            const glm::vec3 n = dist > 1.0e-6f ? d / dist : glm::vec3(0.0f, 1.0f, 0.0f);
                            const float depth = reach - dist;
                            const float total = ia + ib;

                            a.position -= n * (depth * ia / total);
                            b.position += n * (depth * ib / total);

                            glm::vec3 va = a.body ? a.body->velocity : glm::vec3(0.0f);
                            glm::vec3 vb = b.body ? b.body->velocity : glm::vec3(0.0f);
                            const float vn = glm::dot(vb - va, n);
                            if (vn < 0.0f) {
                                const float ea = a.body ? a.body->restitution : 0.0f;
                                const float eb = b.body ? b.body->restitution : 0.0f;
                                const float j2 = -(1.0f + std::min(ea, eb)) * vn / total;
                                if (a.dynamic) a.body->velocity -= n * (j2 * ia);
                                if (b.dynamic) b.body->velocity += n * (j2 * ib);
                            }
                            if (record) collisions.push_back({a.entity, b.entity, n, depth});
                        }
                    }
                }
            }
        }

        for (Body& b : bodies) {
            if (!b.dynamic) continue;
            for (const auto& [planeEntity, plane] : planes) {
                const glm::vec3 n = glm::normalize(plane->normal);
                const float depth = b.radius - glm::dot(n, b.position - world.getWorldPosition(planeEntity));
                if (depth <= 0.0f) continue;
                b.position += n * depth;
                const float vn = glm::dot(b.body->velocity, n);
                if (vn < 0.0f) b.body->velocity -= n * ((1.0f + std::min(b.body->restitution, plane->restitution)) * vn);
                if (record) collisions.push_back({b.entity, planeEntity, -n, depth});
            }
        }
    }

    for (const Body& b : bodies) {
        if (b.dynamic) world.setPosition(b.entity, b.position);
    }
    for (const Collision& c : collisions) world.events().emit(c);
}

void registerPhysicsComponents(World& world) {
    world.registerComponent<RigidBody>(
        "RigidBody",
        [](const RigidBody& r, std::ostream& out) { out << r.velocity.x << ' ' << r.velocity.y << ' ' << r.velocity.z << ' ' << r.mass << ' ' << r.gravityScale << ' ' << r.restitution << ' ' << int(r.isStatic); },
        [](std::istream& in) -> std::optional<RigidBody> {
            RigidBody r;
            int isStatic;
            if (!(in >> r.velocity.x >> r.velocity.y >> r.velocity.z >> r.mass >> r.gravityScale >> r.restitution >> isStatic)) return std::nullopt;
            r.isStatic = isStatic != 0;
            return r;
        });
    world.registerComponent<SphereCollider>(
        "SphereCollider", [](const SphereCollider& c, std::ostream& out) { out << c.radius; },
        [](std::istream& in) -> std::optional<SphereCollider> {
            SphereCollider c;
            if (!(in >> c.radius)) return std::nullopt;
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
