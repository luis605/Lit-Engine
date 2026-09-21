module;

#include <array>
#include <cstdint>
#include <optional>
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
};

export struct SphereCollider {
    float radius = 0.5f;
};

export struct BoxCollider {
    glm::vec3 halfExtents{0.5f};
};

export struct PlaneCollider {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float restitution = 0.0f;
};

export struct Collision {
    EntityHandle a;
    EntityHandle b;
    glm::vec3 normal{0.0f};
    float depth = 0.0f;
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

export void wakeBody(World& world, EntityHandle e);
export std::optional<PhysicsHit> physicsRaycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1.0e30f);

export void stepPhysics(World& world, float dt, const PhysicsSettings& settings = {});
export void registerPhysicsComponents(World& world);
