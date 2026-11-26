module;

#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

module Engine.Physics;

#include "PhysicsShapes.hpp"

using namespace phys_detail;

namespace {
constexpr uint32_t kLayerCount = 32;
constexpr float kDefaultFriction = 0.5f;
constexpr float kSpinInertiaFactor = 0.4f;
constexpr float kMinCastDirection = 1.0e-8f;
constexpr float kMinNormalLength = 1.0e-8f;

using PartList = std::vector<Part>;

struct Body {
    EntityHandle entity;
    glm::vec3 position;
    float boundRadius;
    uint32_t layer;
    RigidBody* body;
    bool dynamic;
    bool asleep;
    PartList parts;
    float spinRadius;
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

void gatherParts(World& world, EntityHandle entity, PartList& parts) {
    if (const BoxCollider* box = world.get<BoxCollider>(entity)) {
        Part part;
        part.kind = ShapeKind::Box;
        part.half = box->halfExtents;
        parts.push_back(part);
    } else if (const SphereCollider* sphere = world.get<SphereCollider>(entity)) {
        Part part;
        part.kind = ShapeKind::Sphere;
        part.radius = sphere->radius;
        parts.push_back(part);
    }
    if (const CapsuleCollider* capsule = world.get<CapsuleCollider>(entity)) {
        Part part;
        part.kind = ShapeKind::Capsule;
        part.radius = capsule->radius;
        part.halfHeight = capsule->halfHeight;
        parts.push_back(part);
    }
    if (const CompoundCollider* compound = world.get<CompoundCollider>(entity)) {
        for (const CompoundPart& source : compound->parts) {
            Part part;
            part.offset = source.offset;
            switch (source.kind) {
            case CompoundPart::Kind::Sphere:
                part.kind = ShapeKind::Sphere;
                part.radius = source.radius;
                break;
            case CompoundPart::Kind::Box:
                part.kind = ShapeKind::Box;
                part.half = source.halfExtents;
                break;
            case CompoundPart::Kind::Capsule:
                part.kind = ShapeKind::Capsule;
                part.radius = source.radius;
                part.halfHeight = source.halfHeight;
                break;
            }
            parts.push_back(part);
        }
    }
    if (const ConvexHullCollider* hull = world.get<ConvexHullCollider>(entity)) {
        if (hull->hull && hull->hull->valid()) {
            Part part;
            part.kind = ShapeKind::Hull;
            part.hull = hull->hull.get();
            part.xf = glm::mat3(world.getWorldMatrix(entity));
            parts.push_back(part);
        }
    }
}

template <class Fn>
void forEachShapeEntity(World& world, Fn&& fn) {
    std::unordered_set<uint32_t> seen;
    PartList parts;
    const auto visit = [&](EntityHandle entity) {
        if (!seen.insert(entity.index).second) return;
        parts.clear();
        gatherParts(world, entity, parts);
        if (!parts.empty()) fn(entity, parts);
    };
    world.view<SphereCollider>([&](EntityHandle entity, SphereCollider&) { visit(entity); });
    world.view<BoxCollider>([&](EntityHandle entity, BoxCollider&) { visit(entity); });
    world.view<CapsuleCollider>([&](EntityHandle entity, CapsuleCollider&) { visit(entity); });
    world.view<CompoundCollider>([&](EntityHandle entity, CompoundCollider&) { visit(entity); });
    world.view<ConvexHullCollider>([&](EntityHandle entity, ConvexHullCollider&) { visit(entity); });
}

// Entity placement of a static surface (heightfield, triangle mesh): rigid part plus uniform scale for distances.
struct Frame {
    glm::mat4 toWorld{1.0f};
    glm::mat4 toLocal{1.0f};
    glm::mat3 normalMatrix{1.0f};
    float scale = 1.0f;

    [[nodiscard]] glm::vec3 point(const glm::vec3& world) const { return glm::vec3(toLocal * glm::vec4(world, 1.0f)); }
    [[nodiscard]] glm::vec3 direction(const glm::vec3& world) const { return glm::vec3(toLocal * glm::vec4(world, 0.0f)); }
    [[nodiscard]] glm::vec3 toWorldPoint(const glm::vec3& local) const { return glm::vec3(toWorld * glm::vec4(local, 1.0f)); }
    [[nodiscard]] glm::vec3 normal(const glm::vec3& local) const {
        const glm::vec3 n = normalMatrix * local;
        const float length = glm::length(n);
        return length > kMinNormalLength ? n / length : glm::vec3(0.0f, 1.0f, 0.0f);
    }
};

Frame makeFrame(const World& world, EntityHandle entity) {
    Frame frame;
    frame.toWorld = world.getWorldMatrix(entity);
    frame.toLocal = glm::inverse(frame.toWorld);
    const glm::mat3 linear(frame.toWorld);
    frame.normalMatrix = glm::transpose(glm::inverse(linear));
    frame.scale = std::max(std::cbrt(std::abs(glm::dot(linear[0], glm::cross(linear[1], linear[2])))), 1.0e-6f);
    return frame;
}

// A surface contact: normal points out of the surface toward the body.
struct SurfaceHit {
    glm::vec3 normal;
    float depth;
};

void fieldCapsule(const Frame& frame, const HeightfieldData& data, const glm::vec3& s0, const glm::vec3& s1, float radius, std::vector<SurfaceHit>& out) {
    const glm::vec3 axis = s1 - s0;
    const float length = glm::length(axis);
    const int samples = length < 1.0e-6f ? 1 : std::clamp(static_cast<int>(std::ceil(length / std::max(radius, 0.05f))) + 1, 2, 32);
    for (int sample = 0; sample < samples; ++sample) {
        const glm::vec3 center = s0 + axis * (samples > 1 ? static_cast<float>(sample) / static_cast<float>(samples - 1) : 0.0f);
        if (const auto contact = heightfieldSphereContact(data, frame.point(center), radius / frame.scale)) out.push_back({frame.normal(contact->normal), contact->depth * frame.scale});
    }
}

void fieldPoint(const Frame& frame, const HeightfieldData& data, const glm::vec3& world, std::vector<SurfaceHit>& out) {
    const glm::vec3 local = frame.point(world);
    if (!heightfieldContains(data, local.x, local.z)) return;
    const float surface = heightfieldHeightAt(data, local.x, local.z);
    const glm::vec3 normal = heightfieldNormalAt(data, local.x, local.z);
    const float depth = (surface - local.y) * normal.y;
    if (depth > 0.0f) out.push_back({frame.normal(normal), depth * frame.scale});
}

void fieldPart(const Frame& frame, const HeightfieldData& data, const Part& part, const glm::vec3& c, std::vector<SurfaceHit>& out) {
    switch (part.kind) {
    case ShapeKind::Sphere:
        if (const auto contact = heightfieldSphereContact(data, frame.point(c), part.radius / frame.scale)) out.push_back({frame.normal(contact->normal), contact->depth * frame.scale});
        break;
    case ShapeKind::Capsule: {
        glm::vec3 s0, s1;
        capsuleSegment(part, c, s0, s1);
        fieldCapsule(frame, data, s0, s1, part.radius, out);
        break;
    }
    case ShapeKind::Box: {
        constexpr int kSamples = 3;
        for (int sz = 0; sz < kSamples; ++sz) {
            for (int sx = 0; sx < kSamples; ++sx) fieldPoint(frame, data, c + glm::vec3(part.half.x * (static_cast<float>(sx) - 1.0f), -part.half.y, part.half.z * (static_cast<float>(sz) - 1.0f)), out);
        }
        break;
    }
    case ShapeKind::Hull:
        for (const glm::vec3& p : part.hull->points) fieldPoint(frame, data, part.xf * p + c, out);
        break;
    }
}

void meshPart(const Frame& frame, const TriangleMeshData& mesh, const Part& part, const glm::vec3& c, std::vector<SurfaceHit>& out) {
    glm::vec3 lo, hi;
    partBounds(part, c, lo, hi);
    glm::vec3 localLo(1.0e30f), localHi(-1.0e30f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 corner((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z);
        const glm::vec3 local = frame.point(corner);
        localLo = glm::min(localLo, local);
        localHi = glm::max(localHi, local);
    }
    std::vector<uint32_t> candidates;
    meshQuery(mesh, localLo, localHi, candidates);
    if (candidates.empty()) return;
    WorldHull hull;
    if (part.kind == ShapeKind::Box || part.kind == ShapeKind::Hull) hull = partHull(part, c);
    for (uint32_t index : candidates) {
        const Triangle t{frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3]]), frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3 + 1]]), frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3 + 2]])};
        const auto contact = partTriangle(part, c, t, &hull);
        if (contact && contact->depth > 0.0f) out.push_back({contact->normal, contact->depth});
    }
}

void meshCapsule(const Frame& frame, const TriangleMeshData& mesh, const glm::vec3& s0, const glm::vec3& s1, float radius, std::vector<SurfaceHit>& out) {
    const glm::vec3 lo = glm::min(s0, s1) - radius;
    const glm::vec3 hi = glm::max(s0, s1) + radius;
    glm::vec3 localLo(1.0e30f), localHi(-1.0e30f);
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 local = frame.point(glm::vec3((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z));
        localLo = glm::min(localLo, local);
        localHi = glm::max(localHi, local);
    }
    std::vector<uint32_t> candidates;
    meshQuery(mesh, localLo, localHi, candidates);
    for (uint32_t index : candidates) {
        const Triangle t{frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3]]), frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3 + 1]]), frame.toWorldPoint(mesh.vertices[mesh.indices[index * 3 + 2]])};
        const auto contact = capsuleTriangle(s0, s1, radius, t);
        if (contact && contact->depth > 0.0f) out.push_back({contact->normal, contact->depth});
    }
}

float planeReach(const Part& part, const glm::vec3& normal, const glm::vec3& center, const glm::vec3& planePoint) {
    switch (part.kind) {
    case ShapeKind::Sphere: return part.radius - glm::dot(normal, center - planePoint);
    case ShapeKind::Box: return std::abs(normal.x) * part.half.x + std::abs(normal.y) * part.half.y + std::abs(normal.z) * part.half.z - glm::dot(normal, center - planePoint);
    case ShapeKind::Capsule: return part.radius + std::abs(normal.y) * part.halfHeight - glm::dot(normal, center - planePoint);
    case ShapeKind::Hull: {
        float deepest = -1.0e30f;
        for (const glm::vec3& p : part.hull->points) deepest = std::max(deepest, -glm::dot(normal, part.xf * p + center - planePoint));
        return deepest;
    }
    }
    return -1.0f;
}

// ----------------------------------------------------------------------------------------------------------------------------
// Contact solver (position based with velocity impulses, Coulomb friction, sphere spin and rolling resistance).

struct Material {
    float restitution = 0.0f;
    float staticFriction = 0.6f;
    float dynamicFriction = 0.5f;
    float rolling = 0.0f;
};

struct Side {
    Body* body = nullptr;
    float invMass = 0.0f;
    float spin = 0.0f;
};

Side sideOf(Body& body) {
    Side side;
    side.body = &body;
    side.invMass = inverseMass(body);
    side.spin = body.spinRadius > 0.0f && side.invMass > 0.0f ? body.spinRadius : 0.0f;
    return side;
}

glm::vec3 velocityOf(const Side& side) { return side.body && side.body->body ? side.body->body->velocity : glm::vec3(0.0f); }
glm::vec3 spinOf(const Side& side) { return side.body && side.body->body && side.spin > 0.0f ? side.body->body->angularVelocity : glm::vec3(0.0f); }

constexpr float kMaxStaticFrictionRatio = 4.0f;

Material surfaceMaterial(const RigidBody& body, float restitution, float friction) {
    const float surface = friction >= 0.0f ? friction : kDefaultFriction;
    Material material;
    material.restitution = std::min(body.restitution, restitution);
    material.dynamicFriction = std::sqrt(std::max(body.friction, 0.0f) * surface);
    const float ratio = std::min(body.staticFriction / std::max(body.friction, 1.0e-6f), kMaxStaticFrictionRatio);
    material.staticFriction = std::max(material.dynamicFriction, material.dynamicFriction * ratio);
    material.rolling = body.rollingResistance;
    return material;
}

// Normal points from a to b. Returns the closing speed before the impulse.
float applyContact(Side& a, Side& b, const glm::vec3& normal, float depth, const Material& material, float dt) {
    const float invSum = a.invMass + b.invMass;
    if (invSum <= 0.0f) return 0.0f;
    if (a.invMass > 0.0f) a.body->position -= normal * (depth * a.invMass / invSum);
    if (b.invMass > 0.0f) b.body->position += normal * (depth * b.invMass / invSum);
    const bool aMoves = a.invMass > 0.0f;
    const bool bMoves = b.invMass > 0.0f;

    const auto relative = [&] { return (velocityOf(b) + glm::cross(spinOf(b), -normal * b.spin)) - (velocityOf(a) + glm::cross(spinOf(a), normal * a.spin)); };
    glm::vec3 relativeVelocity = relative();
    const float normalSpeed = glm::dot(relativeVelocity, normal);
    float normalImpulse = 0.0f;
    if (normalSpeed < 0.0f) {
        normalImpulse = -(1.0f + material.restitution) * normalSpeed / invSum;
        if (aMoves) a.body->body->velocity -= normal * (normalImpulse * a.invMass);
        if (bMoves) b.body->body->velocity += normal * (normalImpulse * b.invMass);
    }
    if (normalImpulse > 0.0f) {
        relativeVelocity = relative();
        const glm::vec3 tangentVelocity = relativeVelocity - normal * glm::dot(relativeVelocity, normal);
        const float slip = glm::length(tangentVelocity);
        if (slip > 1.0e-6f) {
            const float spinTermA = a.spin > 0.0f ? 1.0f / kSpinInertiaFactor : 0.0f;
            const float spinTermB = b.spin > 0.0f ? 1.0f / kSpinInertiaFactor : 0.0f;
            const float effective = a.invMass * (1.0f + spinTermA) + b.invMass * (1.0f + spinTermB);
            const float needed = slip / effective;
            const bool stick = needed <= material.staticFriction * normalImpulse;
            const float tangentImpulse = stick ? needed : material.dynamicFriction * normalImpulse;
            const glm::vec3 tangent = tangentVelocity / slip;
            if (aMoves) {
                a.body->body->velocity += tangent * (tangentImpulse * a.invMass);
                if (a.spin > 0.0f) a.body->body->angularVelocity += glm::cross(normal * a.spin, tangent * tangentImpulse) * (a.invMass / (kSpinInertiaFactor * a.spin * a.spin));
            }
            if (bMoves) {
                b.body->body->velocity -= tangent * (tangentImpulse * b.invMass);
                if (b.spin > 0.0f) b.body->body->angularVelocity += glm::cross(-normal * b.spin, -tangent * tangentImpulse) * (b.invMass / (kSpinInertiaFactor * b.spin * b.spin));
            }
            if (stick && a.spin == 0.0f && b.spin == 0.0f) {
                // Static friction also cancels the slip integrated this step (otherwise bodies creep down slopes).
                const glm::vec3 drift = tangentVelocity * dt;
                if (aMoves) a.body->position += drift * (a.invMass / invSum);
                if (bMoves) b.body->position -= drift * (b.invMass / invSum);
            }
        }
        if (material.rolling > 0.0f) {
            for (Side* side : {&a, &b}) {
                if (side->spin <= 0.0f || side->invMass <= 0.0f) continue;
                glm::vec3& omega = side->body->body->angularVelocity;
                const glm::vec3 rolling = omega - normal * glm::dot(omega, normal);
                const float magnitude = glm::length(rolling);
                if (magnitude < 1.0e-8f) continue;
                const float decrease = std::min(magnitude, material.rolling * normalImpulse * side->invMass / (kSpinInertiaFactor * side->spin));
                omega -= rolling / magnitude * decrease;
            }
        }
    }
    return std::max(0.0f, -glm::dot((velocityOf(b) - velocityOf(a)), normal));
}

struct Solver {
    World& world;
    const PhysicsSettings& settings;
    float dt;
    bool record = false;
    std::vector<Collision> collisions;
};

void wakeIfStruck(Body& body, bool otherActive) {
    if (!body.asleep || !otherActive) return;
    body.asleep = false;
    body.body->sleeping = false;
    body.body->sleepTimer = 0.0f;
}

void resolvePair(Body& first, Body& second, Solver& solver) {
    if (!layersCollide(solver.settings, first.layer, second.layer)) return;
    const bool firstActive = first.dynamic && !first.asleep;
    const bool secondActive = second.dynamic && !second.asleep;
    if (!firstActive && !secondActive) return;

    std::optional<Contact> contact;
    for (const Part& pa : first.parts) {
        for (const Part& pb : second.parts) {
            const auto candidate = collideParts(pa, first.position + pa.offset, pb, second.position + pb.offset);
            if (candidate && (!contact || candidate->depth > contact->depth)) contact = candidate;
        }
    }
    if (!contact) return;
    const glm::vec3 normal = contact->normal;
    const float depth = contact->depth;

    const glm::vec3 firstVelocity = first.body ? first.body->velocity : glm::vec3(0.0f);
    const glm::vec3 secondVelocity = second.body ? second.body->velocity : glm::vec3(0.0f);
    const float closing = glm::dot(secondVelocity - firstVelocity, normal);
    if (closing < -solver.settings.wakeSpeed) {
        wakeIfStruck(first, secondActive);
        wakeIfStruck(second, firstActive);
    }

    Side a = sideOf(first);
    Side b = sideOf(second);
    if (a.invMass + b.invMass == 0.0f) return;
    Material material;
    const RigidBody defaults;
    const RigidBody& ra = first.body ? *first.body : defaults;
    const RigidBody& rb = second.body ? *second.body : defaults;
    material.restitution = std::min(ra.restitution, rb.restitution);
    material.dynamicFriction = std::sqrt(std::max(ra.friction, 0.0f) * std::max(rb.friction, 0.0f));
    material.staticFriction = std::max(material.dynamicFriction, std::sqrt(std::max(ra.staticFriction, 0.0f) * std::max(rb.staticFriction, 0.0f)));
    material.rolling = std::max(ra.rollingResistance, rb.rollingResistance);
    applyContact(a, b, normal, depth, material, solver.dt);
    if (solver.record) solver.collisions.push_back({first.entity, second.entity, normal, depth, std::max(0.0f, -closing)});
}

// Applies a set of contacts of one body against one static surface. Contacts sharing a direction are merged by the depth already pushed out.
void resolveSurface(Body& body, EntityHandle surface, std::vector<SurfaceHit>& hits, float restitution, float friction, Solver& solver) {
    if (hits.empty()) return;
    std::sort(hits.begin(), hits.end(), [](const SurfaceHit& l, const SurfaceHit& r) { return l.depth > r.depth; });
    glm::vec3 pushed(0.0f);
    bool recorded = false;
    for (const SurfaceHit& hit : hits) {
        const float effective = hit.depth - glm::dot(pushed, hit.normal);
        if (effective <= 1.0e-6f) continue;
        Side a = sideOf(body);
        Side b;
        const Material material = surfaceMaterial(*body.body, restitution, friction);
        const float closing = std::max(0.0f, -glm::dot(velocityOf(a), hit.normal));
        applyContact(a, b, -hit.normal, effective, material, solver.dt);
        pushed += hit.normal * effective;
        if (solver.record && !recorded) {
            solver.collisions.push_back({body.entity, surface, -hit.normal, hit.depth, closing});
            recorded = true;
        }
    }
}

void collectBodies(World& world, std::vector<Body>& bodies, float& maxRadius) {
    forEachShapeEntity(world, [&](EntityHandle entity, PartList& parts) {
        RigidBody* rigidBody = world.get<RigidBody>(entity);
        const bool dynamic = rigidBody && !rigidBody->isStatic && world.getParent(entity).isNull();
        float bound = 0.0f;
        for (const Part& part : parts) bound = std::max(bound, glm::length(part.offset) + partBound(part));
        float spinRadius = 0.0f;
        if (dynamic && parts.size() == 1 && parts[0].kind == ShapeKind::Sphere && glm::dot(parts[0].offset, parts[0].offset) == 0.0f) spinRadius = parts[0].radius;
        bodies.push_back({entity, world.getWorldPosition(entity), bound, world.getLayer(entity), rigidBody, dynamic, dynamic && rigidBody->sleeping, parts, spinRadius});
        maxRadius = std::max(maxRadius, bound);
    });
}

void resolvePlanes(std::vector<Body>& bodies, Solver& solver) {
    std::vector<std::pair<EntityHandle, const PlaneCollider*>> planes;
    solver.world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) { planes.emplace_back(entity, &plane); });
    if (planes.empty()) return;
    std::vector<SurfaceHit> hits;
    for (const auto& [planeEntity, plane] : planes) {
        if (glm::length(plane->normal) < kMinNormalLength) continue;
        const glm::vec3 normal = glm::normalize(plane->normal);
        const glm::vec3 planePoint = solver.world.getWorldPosition(planeEntity);
        const uint32_t planeLayer = solver.world.getLayer(planeEntity);
        for (Body& body : bodies) {
            if (!body.dynamic || body.asleep || !layersCollide(solver.settings, body.layer, planeLayer)) continue;
            hits.clear();
            for (const Part& part : body.parts) {
                const float depth = planeReach(part, normal, body.position + part.offset, planePoint);
                if (depth > 0.0f) hits.push_back({normal, depth});
            }
            resolveSurface(body, planeEntity, hits, plane->restitution, plane->friction, solver);
        }
    }
}

void resolveHeightfields(std::vector<Body>& bodies, Solver& solver) {
    std::vector<std::pair<EntityHandle, const HeightfieldCollider*>> fields;
    solver.world.view<HeightfieldCollider>([&](EntityHandle entity, HeightfieldCollider& collider) {
        if (collider.data && collider.data->valid()) fields.emplace_back(entity, &collider);
    });
    std::vector<SurfaceHit> hits;
    for (const auto& [fieldEntity, field] : fields) {
        const Frame frame = makeFrame(solver.world, fieldEntity);
        const uint32_t fieldLayer = solver.world.getLayer(fieldEntity);
        for (Body& body : bodies) {
            if (!body.dynamic || body.asleep || body.entity == fieldEntity || !layersCollide(solver.settings, body.layer, fieldLayer)) continue;
            hits.clear();
            for (const Part& part : body.parts) fieldPart(frame, *field->data, part, body.position + part.offset, hits);
            resolveSurface(body, fieldEntity, hits, field->restitution, field->friction, solver);
        }
    }
}

void resolveMeshes(std::vector<Body>& bodies, Solver& solver) {
    std::vector<std::pair<EntityHandle, const TriMeshCollider*>> meshes;
    solver.world.view<TriMeshCollider>([&](EntityHandle entity, TriMeshCollider& collider) {
        if (collider.data && collider.data->valid()) meshes.emplace_back(entity, &collider);
    });
    std::vector<SurfaceHit> hits;
    for (const auto& [meshEntity, mesh] : meshes) {
        const Frame frame = makeFrame(solver.world, meshEntity);
        const uint32_t meshLayer = solver.world.getLayer(meshEntity);
        for (Body& body : bodies) {
            if (!body.dynamic || body.asleep || body.entity == meshEntity || !layersCollide(solver.settings, body.layer, meshLayer)) continue;
            hits.clear();
            for (const Part& part : body.parts) meshPart(frame, *mesh->data, part, body.position + part.offset, hits);
            resolveSurface(body, meshEntity, hits, mesh->restitution, mesh->friction, solver);
        }
    }
}

void commitBodies(World& world, std::vector<Body>& bodies, float dt, const PhysicsSettings& settings) {
    for (Body& body : bodies) {
        if (!body.dynamic) continue;
        if (body.asleep) {
            body.body->velocity = glm::vec3(0.0f);
            body.body->angularVelocity = glm::vec3(0.0f);
            continue;
        }
        world.setPosition(body.entity, body.position);
        if (body.spinRadius > 0.0f) {
            const float spin = glm::length(body.body->angularVelocity);
            if (spin > 1.0e-6f) world.setRotation(body.entity, glm::normalize(glm::angleAxis(spin * dt, body.body->angularVelocity / spin) * world.getRotation(body.entity)));
        }
        const float activity = std::max(glm::length(body.body->velocity), glm::length(body.body->angularVelocity) * body.spinRadius);
        if (activity >= settings.sleepVelocity) {
            body.body->sleepTimer = 0.0f;
            continue;
        }
        body.body->sleepTimer += dt;
        if (body.body->sleepTimer >= settings.sleepTime) {
            body.body->sleeping = true;
            body.body->velocity = glm::vec3(0.0f);
            body.body->angularVelocity = glm::vec3(0.0f);
        }
    }
}
}

void wakeBody(World& world, EntityHandle entity) {
    if (RigidBody* body = world.get<RigidBody>(entity)) {
        body->sleeping = false;
        body->sleepTimer = 0.0f;
    }
}

namespace {
void advanceWithSweep(World& world, Body& body, float dt, const PhysicsSettings& settings) {
    constexpr float kMinSweepRadius = 1.0e-3f;
    constexpr float kGrazingCosine = 0.05f;
    constexpr float kContactSink = 0.01f;
    const glm::vec3 displacement = body.body->velocity * dt;
    const float travel = glm::length(displacement);
    if (travel <= std::max(body.boundRadius, kMinSweepRadius)) {
        body.position += displacement;
        return;
    }
    const glm::vec3 direction = displacement / travel;
    const auto skip = [&](EntityHandle other) {
        if (other == body.entity) return true;
        const RigidBody* otherBody = world.get<RigidBody>(other);
        return (otherBody && !otherBody->isStatic) || !layersCollide(settings, body.layer, world.getLayer(other));
    };
    const auto hit = physicsSweepSphere(world, body.position, direction, travel, std::max(body.boundRadius, kMinSweepRadius), skip);
    if (!hit || hit->distance >= travel || glm::dot(direction, hit->normal) > -kGrazingCosine) {
        body.position += displacement;
        return;
    }
    const float sweepRadius = std::max(body.boundRadius, kMinSweepRadius);
    body.position += direction * std::min(hit->distance + kContactSink * sweepRadius, travel);
}

}

void stepPhysics(World& world, float dt, const PhysicsSettings& settings) {
    constexpr float kMinCellSize = 0.001f;
    constexpr int kNeighborRadius = 1;
    std::vector<Body> bodies;
    float maxRadius = 0.0f;
    collectBodies(world, bodies, maxRadius);

    for (Body& body : bodies) {
        if (!body.dynamic || body.asleep) continue;
        body.body->velocity += settings.gravity * body.body->gravityScale * dt;
        advanceWithSweep(world, body, dt, settings);
    }

    const float cellSize = std::max(maxRadius * 2.0f, kMinCellSize);
    std::unordered_map<uint64_t, std::vector<uint32_t>> grid;
    grid.reserve(bodies.size());
    const auto cellOf = [&](const glm::vec3& position) {
        return glm::ivec3(static_cast<int>(std::floor(position.x / cellSize)), static_cast<int>(std::floor(position.y / cellSize)), static_cast<int>(std::floor(position.z / cellSize)));
    };

    Solver solver{world, settings, dt, false, {}};
    const int iterations = std::max(1, settings.solverIterations);
    for (int iteration = 0; iteration < iterations; ++iteration) {
        solver.record = iteration == 0;
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
                            if (other > index) resolvePair(bodies[index], bodies[other], solver);
                        }
                    }
                }
            }
        }
        resolvePlanes(bodies, solver);
        resolveHeightfields(bodies, solver);
        resolveMeshes(bodies, solver);
    }

    commitBodies(world, bodies, dt, settings);
    for (const Collision& collision : solver.collisions) world.events().emit(collision);
}

namespace {
using CastCandidate = std::function<void(EntityHandle, float, const glm::vec3&)>;

struct RayHit {
    float t;
    glm::vec3 normal;
};

std::optional<RayHit> raySphere(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& center, float radius) {
    const glm::vec3 toCenter = center - origin;
    const float along = glm::dot(toCenter, direction);
    const float perpendicularSquared = glm::dot(toCenter, toCenter) - along * along;
    const float reachSquared = radius * radius;
    if (perpendicularSquared > reachSquared) return std::nullopt;
    const float halfChord = std::sqrt(reachSquared - perpendicularSquared);
    float distance = along - halfChord;
    if (distance < 0.0f) distance = along + halfChord;
    const glm::vec3 offset = origin + direction * distance - center;
    return RayHit{distance, glm::length(offset) > kMinCastDirection ? glm::normalize(offset) : -direction};
}

std::optional<RayHit> rayBox(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& center, const glm::vec3& half, float inflate) {
    const glm::vec3 low = center - half - inflate;
    const glm::vec3 high = center + half + inflate;
    float enter = 0.0f;
    float leave = std::numeric_limits<float>::max();
    int hitAxis = -1;
    float hitSign = 0.0f;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < kMinCastDirection) {
            if (origin[axis] < low[axis] || origin[axis] > high[axis]) return std::nullopt;
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
        if (enter > leave) return std::nullopt;
    }
    glm::vec3 normal(0.0f);
    if (hitAxis >= 0) normal[hitAxis] = hitSign;
    return RayHit{enter, normal};
}

std::optional<RayHit> rayCapsule(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& s0, const glm::vec3& s1, float radius) {
    float best = std::numeric_limits<float>::max();
    glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
    const glm::vec3 axis = s1 - s0;
    const float length = glm::length(axis);
    const glm::vec3 m = origin - s0;
    if (length > 1.0e-6f) {
        const glm::vec3 u = axis / length;
        const glm::vec3 dPerp = direction - u * glm::dot(direction, u);
        const glm::vec3 mPerp = m - u * glm::dot(m, u);
        const float qa = glm::dot(dPerp, dPerp);
        const float qc = glm::dot(mPerp, mPerp) - radius * radius;
        const float alongOrigin = glm::dot(m, u);
        if (qc <= 0.0f && alongOrigin >= 0.0f && alongOrigin <= length) return RayHit{0.0f, -direction};
        if (qa > 1.0e-10f) {
            const float qb = glm::dot(dPerp, mPerp);
            const float disc = qb * qb - qa * qc;
            if (disc >= 0.0f) {
                const float t = (-qb - std::sqrt(disc)) / qa;
                const float along = glm::dot(m + direction * t, u);
                if (t >= 0.0f && along >= 0.0f && along <= length) {
                    best = t;
                    bestNormal = (origin + direction * t - (s0 + u * along)) / radius;
                }
            }
        }
    }
    for (const glm::vec3& center : {s0, s1}) {
        const glm::vec3 mc = origin - center;
        const float b = glm::dot(mc, direction);
        const float c = glm::dot(mc, mc) - radius * radius;
        if (c <= 0.0f) return RayHit{0.0f, -direction};
        if (b > 0.0f) continue;
        const float disc = b * b - c;
        if (disc < 0.0f) continue;
        const float t = -b - std::sqrt(disc);
        if (t >= 0.0f && t < best) {
            best = t;
            bestNormal = (origin + direction * t - center) / radius;
        }
    }
    if (best == std::numeric_limits<float>::max()) return std::nullopt;
    return RayHit{best, bestNormal};
}

std::optional<RayHit> rayHull(const glm::vec3& origin, const glm::vec3& direction, const WorldHull& hull, float inflate) {
    float enter = 0.0f;
    float leave = std::numeric_limits<float>::max();
    glm::vec3 normal = -direction;
    for (const WorldHull::Face& face : hull.faces) {
        const float denominator = glm::dot(face.n, direction);
        const float distance = glm::dot(face.n, origin) - (face.d + inflate);
        if (std::abs(denominator) < kMinCastDirection) {
            if (distance > 0.0f) return std::nullopt;
            continue;
        }
        const float t = -distance / denominator;
        if (denominator < 0.0f) {
            if (t > enter) {
                enter = t;
                normal = face.n;
            }
        } else {
            leave = std::min(leave, t);
        }
        if (enter > leave) return std::nullopt;
    }
    return RayHit{enter, normal};
}

void castShapes(World& world, const glm::vec3& origin, const glm::vec3& direction, float inflate, const CastCandidate& consider) {
    forEachShapeEntity(world, [&](EntityHandle entity, PartList& parts) {
        const glm::vec3 base = world.getWorldPosition(entity);
        for (const Part& part : parts) {
            const glm::vec3 center = base + part.offset;
            std::optional<RayHit> hit;
            switch (part.kind) {
            case ShapeKind::Sphere: hit = raySphere(origin, direction, center, part.radius + inflate); break;
            case ShapeKind::Box: hit = rayBox(origin, direction, center, part.half, inflate); break;
            case ShapeKind::Capsule: {
                glm::vec3 s0, s1;
                capsuleSegment(part, center, s0, s1);
                hit = rayCapsule(origin, direction, s0, s1, part.radius + inflate);
                break;
            }
            case ShapeKind::Hull: hit = rayHull(origin, direction, partHull(part, center), inflate); break;
            }
            if (hit) consider(entity, hit->t, hit->normal);
        }
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

void castHeightfields(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float inflate, const CastCandidate& consider) {
    world.view<HeightfieldCollider>([&](EntityHandle entity, HeightfieldCollider& collider) {
        if (!collider.data || !collider.data->valid()) return;
        const Frame frame = makeFrame(world, entity);
        const glm::vec3 localDirection = frame.direction(direction);
        const float stretch = glm::length(localDirection);
        if (stretch < kMinCastDirection) return;
        const glm::vec3 local = frame.point(origin);
        const float localMax = maxDistance * stretch;
        const auto hit = inflate > 0.0f ? heightfieldSweepSphere(*collider.data, local, localDirection, localMax, inflate / frame.scale) : heightfieldRaycast(*collider.data, local, localDirection, localMax);
        if (hit) consider(entity, hit->distance / stretch, frame.normal(hit->normal));
    });
}

void castMeshes(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float inflate, const CastCandidate& consider) {
    world.view<TriMeshCollider>([&](EntityHandle entity, TriMeshCollider& collider) {
        if (!collider.data || !collider.data->valid()) return;
        const Frame frame = makeFrame(world, entity);
        const glm::vec3 localDirection = frame.direction(direction);
        const float stretch = glm::length(localDirection);
        if (stretch < kMinCastDirection) return;
        const glm::vec3 local = frame.point(origin);
        const float localMax = maxDistance * stretch;
        const auto hit = inflate > 0.0f ? meshSweepSphere(*collider.data, local, localDirection, localMax, inflate / frame.scale) : meshRaycast(*collider.data, local, localDirection, localMax);
        if (hit) consider(entity, hit->distance / stretch, frame.normal(hit->normal));
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
    castShapes(world, origin, unitDirection, inflate, consider);
    castPlanes(world, origin, unitDirection, inflate, consider);
    castHeightfields(world, origin, unitDirection, maxDistance, inflate, consider);
    castMeshes(world, origin, unitDirection, maxDistance, inflate, consider);
    return best;
}

// Overlap query: a capsule segment (a == b is a sphere) or an axis aligned part (box).
struct Query {
    bool capsule = true;
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    float radius = 0.0f;
    Part part;
    glm::vec3 center{0.0f};
};

std::optional<Contact> queryVersusPart(const Query& query, const Part& target, const glm::vec3& tc) {
    if (!query.capsule) return collideParts(target, tc, query.part, query.center);
    // Normal returned points from the collider toward the query.
    switch (target.kind) {
    case ShapeKind::Sphere: {
        const glm::vec3 offset = closestOnSegment(query.a, query.b, tc) - tc;
        const float reach = query.radius + target.radius;
        const float distance = glm::length(offset);
        if (distance >= reach) return std::nullopt;
        return Contact{distance > 1.0e-6f ? offset / distance : glm::vec3(0.0f, 1.0f, 0.0f), reach - distance};
    }
    case ShapeKind::Box: {
        auto c = capsuleBox(query.a, query.b, query.radius, tc, target.half);
        if (c) c->normal = -c->normal;
        return c;
    }
    case ShapeKind::Capsule: {
        glm::vec3 t0, t1, c1, c2;
        capsuleSegment(target, tc, t0, t1);
        closestSegmentSegment(query.a, query.b, t0, t1, c1, c2);
        return sphereSphere(c2, target.radius, c1, query.radius);
    }
    case ShapeKind::Hull: {
        auto c = capsuleHull(query.a, query.b, query.radius, partHull(target, tc));
        if (c) c->normal = -c->normal;
        return c;
    }
    }
    return std::nullopt;
}

std::vector<PhysicsContact> overlapImpl(World& world, const Query& query, const PhysicsFilter& skip) {
    std::vector<PhysicsContact> contacts;
    const auto add = [&](EntityHandle entity, const glm::vec3& normal, float depth) {
        if (depth > 0.0f) contacts.push_back({entity, normal, depth});
    };
    forEachShapeEntity(world, [&](EntityHandle entity, PartList& parts) {
        if (skip && skip(entity)) return;
        const glm::vec3 base = world.getWorldPosition(entity);
        for (const Part& part : parts) {
            const auto contact = queryVersusPart(query, part, base + part.offset);
            if (contact) add(entity, contact->normal, contact->depth);
        }
    });
    world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) {
        if ((skip && skip(entity)) || glm::length(plane.normal) < kMinNormalLength) return;
        const glm::vec3 normal = glm::normalize(plane.normal);
        const glm::vec3 planePoint = world.getWorldPosition(entity);
        const float depth = query.capsule ? query.radius - std::min(glm::dot(normal, query.a - planePoint), glm::dot(normal, query.b - planePoint)) : planeReach(query.part, normal, query.center, planePoint);
        add(entity, normal, depth);
    });
    std::vector<SurfaceHit> hits;
    world.view<HeightfieldCollider>([&](EntityHandle entity, HeightfieldCollider& collider) {
        if ((skip && skip(entity)) || !collider.data || !collider.data->valid()) return;
        hits.clear();
        const Frame frame = makeFrame(world, entity);
        if (query.capsule) fieldCapsule(frame, *collider.data, query.a, query.b, query.radius, hits);
        else fieldPart(frame, *collider.data, query.part, query.center, hits);
        for (const SurfaceHit& hit : hits) add(entity, hit.normal, hit.depth);
    });
    world.view<TriMeshCollider>([&](EntityHandle entity, TriMeshCollider& collider) {
        if ((skip && skip(entity)) || !collider.data || !collider.data->valid()) return;
        hits.clear();
        const Frame frame = makeFrame(world, entity);
        if (query.capsule) meshCapsule(frame, *collider.data, query.a, query.b, query.radius, hits);
        else meshPart(frame, *collider.data, query.part, query.center, hits);
        for (const SurfaceHit& hit : hits) add(entity, hit.normal, hit.depth);
    });
    return contacts;
}
}

std::optional<PhysicsHit> physicsRaycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip) {
    return castImpl(world, origin, direction, maxDistance, 0.0f, skip);
}

std::optional<PhysicsHit> physicsSweepSphere(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius, const PhysicsFilter& skip) {
    return castImpl(world, origin, direction, maxDistance, radius, skip);
}

std::optional<PhysicsHit> physicsSweepCapsule(World& world, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip) {
    const glm::vec3 axis = b - a;
    const int samples = std::clamp(static_cast<int>(std::ceil(glm::length(axis) / std::max(radius, 0.05f))) + 1, 2, 32);
    std::optional<PhysicsHit> best;
    for (int sample = 0; sample < samples; ++sample) {
        const glm::vec3 start = a + axis * (static_cast<float>(sample) / static_cast<float>(samples - 1));
        const auto hit = castImpl(world, start, direction, maxDistance, radius, skip);
        if (hit && (!best || hit->distance < best->distance)) best = hit;
    }
    return best;
}

std::vector<PhysicsContact> physicsOverlapBox(World& world, const glm::vec3& center, const glm::vec3& halfExtents, const PhysicsFilter& skip) {
    Query query;
    query.capsule = false;
    query.part.kind = ShapeKind::Box;
    query.part.half = halfExtents;
    query.center = center;
    return overlapImpl(world, query, skip);
}

std::optional<PhysicsHit> physicsSweepBox(World& world, const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip) {
    const float length = glm::length(direction);
    if (length < kMinCastDirection) return std::nullopt;
    const glm::vec3 dir = direction / length;
    const auto deepest = [&](float t) -> std::optional<PhysicsHit> {
        const auto contacts = physicsOverlapBox(world, center + dir * t, halfExtents, skip);
        std::optional<PhysicsHit> best;
        float bestDepth = 0.0f;
        for (const PhysicsContact& contact : contacts) {
            if (contact.depth > bestDepth) {
                bestDepth = contact.depth;
                best = PhysicsHit{contact.entity, t, contact.normal};
            }
        }
        return best;
    };
    if (auto start = deepest(0.0f)) return start;
    const float step = std::clamp(std::min({halfExtents.x, halfExtents.y, halfExtents.z}) * 0.5f, 0.01f, 0.5f);
    float previous = 0.0f;
    for (float t = step;; t += step) {
        const float at = std::min(t, maxDistance);
        if (auto hit = deepest(at)) {
            float lo = previous;
            float hi = at;
            for (int i = 0; i < 14; ++i) {
                const float mid = 0.5f * (lo + hi);
                if (deepest(mid)) hi = mid;
                else lo = mid;
            }
            auto result = deepest(hi);
            if (!result) result = hit;
            result->distance = lo;
            return result;
        }
        if (at >= maxDistance) return std::nullopt;
        previous = at;
    }
}

std::vector<PhysicsContact> physicsOverlapCapsule(World& world, const glm::vec3& capsuleStart, const glm::vec3& capsuleEnd, float radius, const PhysicsFilter& skip) {
    Query query;
    query.a = capsuleStart;
    query.b = capsuleEnd;
    query.radius = radius;
    return overlapImpl(world, query, skip);
}

void registerPhysicsComponents(World& world) {
    world.registerComponent<RigidBody>(
        "RigidBody",
        [](const RigidBody& body, std::ostream& output) { output << body.velocity.x << ' ' << body.velocity.y << ' ' << body.velocity.z << ' ' << body.mass << ' ' << body.gravityScale << ' ' << body.restitution << ' ' << int(body.isStatic) << ' ' << int(body.sleeping) << ' ' << body.friction << ' ' << body.staticFriction << ' ' << body.rollingResistance << ' ' << body.angularVelocity.x << ' ' << body.angularVelocity.y << ' ' << body.angularVelocity.z; },
        [](std::istream& input) -> std::optional<RigidBody> {
            RigidBody body;
            int isStatic = 0;
            if (!(input >> body.velocity.x >> body.velocity.y >> body.velocity.z >> body.mass >> body.gravityScale >> body.restitution >> isStatic)) return std::nullopt;
            body.isStatic = isStatic != 0;
            int sleeping = 0;
            if (input >> sleeping) body.sleeping = sleeping != 0;
            if (input >> body.friction >> body.staticFriction >> body.rollingResistance >> body.angularVelocity.x >> body.angularVelocity.y >> body.angularVelocity.z) return body;
            body.friction = 0.5f;
            body.staticFriction = 0.6f;
            body.rollingResistance = 0.0f;
            body.angularVelocity = glm::vec3(0.0f);
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
        "PlaneCollider", [](const PlaneCollider& collider, std::ostream& output) { output << collider.normal.x << ' ' << collider.normal.y << ' ' << collider.normal.z << ' ' << collider.restitution << ' ' << collider.friction; },
        [](std::istream& input) -> std::optional<PlaneCollider> {
            PlaneCollider collider;
            if (!(input >> collider.normal.x >> collider.normal.y >> collider.normal.z >> collider.restitution)) return std::nullopt;
            if (!(input >> collider.friction)) collider.friction = -1.0f;
            return collider;
        });
    world.registerComponent<CapsuleCollider>(
        "CapsuleCollider", [](const CapsuleCollider& collider, std::ostream& output) { output << collider.radius << ' ' << collider.halfHeight; },
        [](std::istream& input) -> std::optional<CapsuleCollider> {
            CapsuleCollider collider;
            if (!(input >> collider.radius >> collider.halfHeight)) return std::nullopt;
            return collider;
        });
    // Hull colliders were not serialized at all, so save / load and snapshots silently dropped them (found by bent,
    // persistence.bent save_load_then_simulate). The hull is stored as its vertices and rebuilt on load.
    world.registerComponent<ConvexHullCollider>(
        "ConvexHullCollider",
        [](const ConvexHullCollider& collider, std::ostream& output) {
            const size_t count = collider.hull ? collider.hull->points.size() : 0;
            output << count;
            for (size_t i = 0; i < count; ++i) output << ' ' << collider.hull->points[i].x << ' ' << collider.hull->points[i].y << ' ' << collider.hull->points[i].z;
        },
        [](std::istream& input) -> std::optional<ConvexHullCollider> {
            size_t count = 0;
            if (!(input >> count) || count > 128) return std::nullopt;
            std::vector<glm::vec3> points(count);
            for (glm::vec3& point : points) {
                if (!(input >> point.x >> point.y >> point.z)) return std::nullopt;
            }
            ConvexHullCollider collider;
            collider.hull = makeConvexHull(points);
            if (!collider.hull) return std::nullopt;
            return collider;
        });
    world.registerComponent<CompoundCollider>(
        "CompoundCollider",
        [](const CompoundCollider& collider, std::ostream& output) {
            output << collider.parts.size();
            for (const CompoundPart& part : collider.parts) output << ' ' << static_cast<int>(part.kind) << ' ' << part.offset.x << ' ' << part.offset.y << ' ' << part.offset.z << ' ' << part.radius << ' ' << part.halfExtents.x << ' ' << part.halfExtents.y << ' ' << part.halfExtents.z << ' ' << part.halfHeight;
        },
        [](std::istream& input) -> std::optional<CompoundCollider> {
            CompoundCollider collider;
            size_t count = 0;
            if (!(input >> count) || count > 4096) return std::nullopt;
            for (size_t i = 0; i < count; ++i) {
                CompoundPart part;
                int kind = 0;
                if (!(input >> kind >> part.offset.x >> part.offset.y >> part.offset.z >> part.radius >> part.halfExtents.x >> part.halfExtents.y >> part.halfExtents.z >> part.halfHeight)) return std::nullopt;
                if (kind < 0 || kind > 2) return std::nullopt;
                part.kind = static_cast<CompoundPart::Kind>(kind);
                collider.parts.push_back(part);
            }
            return collider;
        });
    // Triangle mesh and height field colliders were not serialized either (found by bent, ext_phys_dynamics.bent
    // px_every_collider_survives_save_load): save / load, snapshots and the save system silently dropped terrain and mesh colliders.
    world.registerComponent<TriMeshCollider>(
        "TriMeshCollider",
        [](const TriMeshCollider& collider, std::ostream& output) {
            const TriangleMeshData* mesh = collider.data.get();
            output << collider.restitution << ' ' << collider.friction << ' ' << (mesh ? mesh->vertices.size() : 0) << ' ' << (mesh ? mesh->indices.size() : 0);
            if (!mesh) return;
            for (const glm::vec3& vertex : mesh->vertices) output << ' ' << vertex.x << ' ' << vertex.y << ' ' << vertex.z;
            for (const uint32_t index : mesh->indices) output << ' ' << index;
        },
        [](std::istream& input) -> std::optional<TriMeshCollider> {
            TriMeshCollider collider;
            size_t vertexCount = 0;
            size_t indexCount = 0;
            if (!(input >> collider.restitution >> collider.friction >> vertexCount >> indexCount) || vertexCount > (1u << 24) || indexCount > (1u << 26)) return std::nullopt;
            std::vector<glm::vec3> vertices(vertexCount);
            for (glm::vec3& vertex : vertices) {
                if (!(input >> vertex.x >> vertex.y >> vertex.z)) return std::nullopt;
            }
            std::vector<uint32_t> indices(indexCount);
            for (uint32_t& index : indices) {
                if (!(input >> index) || index >= vertexCount) return std::nullopt;
            }
            collider.data = makeTriangleMesh(std::move(vertices), std::move(indices));
            if (!collider.data) return std::nullopt;
            return collider;
        });
    world.registerComponent<HeightfieldCollider>(
        "HeightfieldCollider",
        [](const HeightfieldCollider& collider, std::ostream& output) {
            const HeightfieldData* field = collider.data.get();
            output << collider.restitution << ' ' << collider.friction << ' ' << (field ? field->samplesX : 0) << ' ' << (field ? field->samplesZ : 0) << ' ' << (field ? field->cellSize : 1.0f);
            if (!field) return;
            for (const float height : field->heights) output << ' ' << height;
        },
        [](std::istream& input) -> std::optional<HeightfieldCollider> {
            HeightfieldCollider collider;
            auto field = std::make_shared<HeightfieldData>();
            if (!(input >> collider.restitution >> collider.friction >> field->samplesX >> field->samplesZ >> field->cellSize)) return std::nullopt;
            if (field->samplesX < 0 || field->samplesZ < 0 || static_cast<int64_t>(field->samplesX) * field->samplesZ > (1 << 24)) return std::nullopt;
            field->heights.resize(static_cast<size_t>(field->samplesX) * static_cast<size_t>(field->samplesZ));
            for (float& height : field->heights) {
                if (!(input >> height)) return std::nullopt;
            }
            if (!field->valid()) return std::nullopt;
            field->updateBounds();
            collider.data = std::move(field);
            return collider;
        });
}
