module;

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

export module Engine.TestScenes;

import Engine.engine;
import Engine.glm;

export struct TestSceneKit {
    uint32_t cube = 0;
    uint32_t sphere = 0;
    uint32_t floorShader = 0;
    std::function<uint32_t(const glm::vec3& color, const glm::vec3& emissive, float emissiveIntensity, float roughness)> material;
};

export struct TestSceneMetrics {
    std::vector<std::pair<std::string, double>> values;
    void set(const std::string& name, double value);
    [[nodiscard]] double get(const std::string& name, double fallback = 0.0) const;
    [[nodiscard]] std::string toJson() const;
};

export struct TestSceneInfo {
    std::string name;
    glm::vec3 start{0.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
    float nearPlane = 0.1f;
    float farPlane = 2000.0f;
    uint32_t suggestedTicks = 600;
    std::function<TestSceneMetrics()> metrics;
};

export [[nodiscard]] std::vector<std::string> testSceneNames();
export bool buildTestScene(Engine& engine, const std::string& name, const TestSceneKit& kit, TestSceneInfo& info);
export void clearTestSceneSystems(Engine& engine);
export void applyTestSceneCamera(Engine& engine, const TestSceneInfo& info);
export [[nodiscard]] double forwardDepthStep(float nearPlane, float farPlane, float distance);
