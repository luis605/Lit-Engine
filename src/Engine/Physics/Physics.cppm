module;

#include <cstdint>
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
};

export struct SphereCollider {
    float radius = 0.5f;
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
};

export void stepPhysics(World& world, float dt, const PhysicsSettings& settings = {});
export void registerPhysicsComponents(World& world);
