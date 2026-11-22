module;

#include <cstdint>
#include <functional>

export module Engine.IkTestScene;

import Engine.engine;
import Engine.glm;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.material;

export struct IkTestKit {
    uint32_t cube = 0;
    uint32_t sphere = 0;
    std::function<uint32_t(const Material&)> material;
    bool debugDraw = false;
};

export struct IkTestSpawn {
    glm::vec3 feet{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

export inline constexpr const char* kIkTestSpiderName = "spider";
export inline constexpr const char* kIkTestQuadrupedName = "quadruped";
export inline constexpr const char* kIkTestBipedName = "biped";
export inline constexpr const char* kIkTestBipedGoalName = "biped goal";
export inline constexpr const char* kIkTestLookTargetName = "look target";
export inline constexpr const char* kIkTestPatrolPrefix = "patrol ";
export inline constexpr const char* kIkTestPushableTag = "ik.pushable";

export void buildIkTestScene(Engine& engine, const IkTestKit& kit);
export [[nodiscard]] IkTestSpawn ikTestSpawn();
export void enableIkTestPlay(Engine& engine, const IkTestKit& kit);

export [[nodiscard]] uint32_t ikTestPaint(const IkTestKit& kit, const glm::vec3& color, float roughness, float metallic = 0.0f);
export [[nodiscard]] uint32_t ikTestGlow(const IkTestKit& kit, const glm::vec3& color, float intensity);
export EntityHandle ikTestStaticBox(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& minCorner, const glm::vec3& size);
export EntityHandle ikTestStaticBump(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& center, float radius);
export EntityHandle ikTestPushableSphere(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& center, float radius);
export void buildIkTestTerrain(World& world, const IkTestKit& kit);
export void buildIkTestRigs(World& world, const IkTestKit& kit);
