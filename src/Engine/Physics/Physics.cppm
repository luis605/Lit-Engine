module;

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <unordered_map>
#include <vector>

export module Engine.Physics;

import Engine.Render.entity;
import Engine.World;
import Engine.glm;

export struct RigidBody {
    glm::vec3 velocity{0.0f};
    float mass = 1.0f;
    float gravityScale = 1.0f;
    float restitution = 0.0f;
    bool isStatic = false;
    bool sleeping = false;
    float sleepTimer = 0.0f;
    float friction = 0.5f;
    float staticFriction = 0.6f;
    float rollingResistance = 0.0f;
    glm::vec3 angularVelocity{0.0f};
};

export struct SphereCollider {
    float radius = 0.5f;
};

export struct BoxCollider {
    glm::vec3 halfExtents{0.5f};
};

export struct CapsuleCollider {
    float radius = 0.5f;
    float halfHeight = 0.5f;
};

export struct ConvexHullData {
    struct Face {
        glm::vec3 normal{0.0f, 1.0f, 0.0f};
        float d = 0.0f;
        std::vector<uint32_t> indices;
    };
    std::vector<glm::vec3> points;
    std::vector<Face> faces;
    std::vector<std::pair<uint32_t, uint32_t>> edges;
    [[nodiscard]] bool valid() const { return points.size() >= 4 && faces.size() >= 4; }
};

export struct ConvexHullCollider {
    std::shared_ptr<const ConvexHullData> hull;
};

export [[nodiscard]] std::shared_ptr<const ConvexHullData> makeConvexHull(const std::vector<glm::vec3>& points);

export struct CompoundPart {
    enum class Kind : uint8_t { Sphere, Box, Capsule };
    Kind kind = Kind::Sphere;
    glm::vec3 offset{0.0f};
    float radius = 0.5f;
    glm::vec3 halfExtents{0.5f};
    float halfHeight = 0.5f;
};

export struct CompoundCollider {
    std::vector<CompoundPart> parts;
};

export struct PlaneCollider {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float restitution = 0.0f;
    float friction = -1.0f;
};

export struct TriangleMeshData {
    struct Node {
        glm::vec3 lo{0.0f};
        glm::vec3 hi{0.0f};
        uint32_t first = 0;
        uint32_t count = 0;
        uint32_t left = 0;
    };
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;
    std::vector<Node> nodes;
    std::vector<uint32_t> order;
    void build();
    [[nodiscard]] bool valid() const { return !nodes.empty() && indices.size() >= 3; }
    [[nodiscard]] size_t triangleCount() const { return indices.size() / 3; }
};

export struct TriMeshCollider {
    std::shared_ptr<const TriangleMeshData> data;
    float restitution = 0.0f;
    float friction = -1.0f;
};

export struct MeshHit {
    float distance = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    uint32_t triangle = 0;
};

export struct MeshContact {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float depth = 0.0f;
    glm::vec3 point{0.0f};
    uint32_t triangle = 0;
};

export [[nodiscard]] std::shared_ptr<const TriangleMeshData> makeTriangleMesh(std::vector<glm::vec3> vertices, std::vector<uint32_t> indices);
export void meshQuery(const TriangleMeshData& mesh, const glm::vec3& lo, const glm::vec3& hi, std::vector<uint32_t>& triangles);
export [[nodiscard]] std::optional<MeshHit> meshRaycast(const TriangleMeshData& mesh, const glm::vec3& origin, const glm::vec3& direction, float maxDistance);
export [[nodiscard]] std::optional<MeshHit> meshSweepSphere(const TriangleMeshData& mesh, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius);
export [[nodiscard]] std::vector<MeshContact> meshSphereContacts(const TriangleMeshData& mesh, const glm::vec3& center, float radius);

export struct HeightfieldData {
    int samplesX = 0;
    int samplesZ = 0;
    float cellSize = 1.0f;
    float minHeight = 0.0f;
    float maxHeight = 0.0f;
    std::vector<float> heights;

    [[nodiscard]] bool valid() const { return samplesX >= 2 && samplesZ >= 2 && heights.size() == static_cast<size_t>(samplesX) * static_cast<size_t>(samplesZ); }
    [[nodiscard]] float at(int x, int z) const { return heights[static_cast<size_t>(z) * static_cast<size_t>(samplesX) + static_cast<size_t>(x)]; }
    [[nodiscard]] float extentX() const { return static_cast<float>(samplesX - 1) * cellSize; }
    [[nodiscard]] float extentZ() const { return static_cast<float>(samplesZ - 1) * cellSize; }
    void updateBounds();
};

export struct HeightfieldCollider {
    std::shared_ptr<const HeightfieldData> data;
    float restitution = 0.0f;
    float friction = -1.0f;
};

export struct HeightfieldHit {
    float distance = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

export struct HeightfieldContact {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float depth = 0.0f;
    glm::vec3 point{0.0f};
};

export [[nodiscard]] bool heightfieldContains(const HeightfieldData& field, float x, float z);
export [[nodiscard]] float heightfieldHeightAt(const HeightfieldData& field, float x, float z);
export [[nodiscard]] glm::vec3 heightfieldNormalAt(const HeightfieldData& field, float x, float z);
export [[nodiscard]] std::optional<HeightfieldHit> heightfieldRaycast(const HeightfieldData& field, const glm::vec3& origin, const glm::vec3& direction, float maxDistance);
export [[nodiscard]] std::optional<HeightfieldContact> heightfieldSphereContact(const HeightfieldData& field, const glm::vec3& center, float radius);
export [[nodiscard]] std::optional<HeightfieldHit> heightfieldSweepSphere(const HeightfieldData& field, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius);

export struct Collision {
    EntityHandle a;
    EntityHandle b;
    glm::vec3 normal{0.0f};
    float depth = 0.0f;
    float closingSpeed = 0.0f;
};

export struct PhysicsSettings {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    int solverIterations = 2;
    float sleepVelocity = 0.1f;
    float sleepTime = 0.5f;
    float wakeSpeed = 0.5f;
    std::array<uint32_t, 32> collisionMatrix = [] {
        std::array<uint32_t, 32> m{};
        m.fill(0xFFFFFFFFu);
        return m;
    }();

    void setLayersCollide(uint32_t layerA, uint32_t layerB, bool collide) {
        if (layerA >= 32 || layerB >= 32) return;
        if (collide) {
            collisionMatrix[layerA] |= 1u << layerB;
            collisionMatrix[layerB] |= 1u << layerA;
        } else {
            collisionMatrix[layerA] &= ~(1u << layerB);
            collisionMatrix[layerB] &= ~(1u << layerA);
        }
    }
};

export struct PhysicsHit {
    EntityHandle entity;
    float distance = 0.0f;
    glm::vec3 normal{0.0f};
};

export struct PhysicsContact {
    EntityHandle entity;
    glm::vec3 normal{0.0f};
    float depth = 0.0f;
};

export using PhysicsFilter = std::function<bool(EntityHandle)>;

export void wakeBody(World& world, EntityHandle entity);
export std::optional<PhysicsHit> physicsRaycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1.0e30f, const PhysicsFilter& skip = {});
export std::optional<PhysicsHit> physicsSweepSphere(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius, const PhysicsFilter& skip = {});
export std::optional<PhysicsHit> physicsSweepCapsule(World& world, const glm::vec3& a, const glm::vec3& b, float radius, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip = {});
export std::optional<PhysicsHit> physicsSweepBox(World& world, const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec3& direction, float maxDistance, const PhysicsFilter& skip = {});
export std::vector<PhysicsContact> physicsOverlapBox(World& world, const glm::vec3& center, const glm::vec3& halfExtents, const PhysicsFilter& skip = {});
export std::vector<PhysicsContact> physicsOverlapCapsule(World& world, const glm::vec3& a, const glm::vec3& b, float radius, const PhysicsFilter& skip = {});

export void stepPhysics(World& world, float dt, const PhysicsSettings& settings = {});
export void registerPhysicsComponents(World& world);
