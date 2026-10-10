module;

#include <optional>
#include <vector>

export module Engine.PortalPhysics;

import Engine.glm;
import Engine.Render.entity;
import Engine.World;
import Engine.Portal;
import Engine.Physics;

export struct PortalCast {
    std::optional<PhysicsHit> hit;
    glm::vec3 point{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
    float distance = 0.0f;
    int cell = -1;
    int crossings = 0;
};

export [[nodiscard]] PortalCast portalSweep(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius = 0.0f, int cell = -1, const PhysicsFilter& skip = {}, bool collide = true, int maxCrossings = 8);
export [[nodiscard]] PortalCast portalRaycast(const PortalSystem& portals, World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell = -1, const PhysicsFilter& skip = {});
export [[nodiscard]] std::vector<PortalOverlap> portalOverlap(const PortalSystem& portals, World& world, const glm::vec3& center, float radius, int cell = -1, const PhysicsFilter& skip = {});

export void installPortalPhysics(PortalSystem& portals);
export void stepPortalPhysics(PortalSystem& portals, World& world, float dt, const PhysicsSettings& settings = {});
