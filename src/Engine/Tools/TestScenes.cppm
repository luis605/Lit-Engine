module;

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

export module Engine.TestScenes;

import Engine.engine;
import Engine.glm;
import Engine.Render.material;

export struct TestSceneKit {
    uint32_t cube = 0;
    uint32_t sphere = 0;
    uint32_t floorShader = 0;
    std::function<uint32_t(const glm::vec3& color, const glm::vec3& emissive, float emissiveIntensity, float roughness)> material;
    std::function<uint32_t(const Material&)> materialFull;
};

export struct LargeWorldConfig {
    double originKm = 500.0;
    float flySpeed = 0.0f;
    int rebaseFrame = 0;
    double rebaseThreshold = 400.0;
    double rebaseSnap = 256.0;
    bool autoRebase = true;
};

export struct TestSceneParams {
    float lightIntensity = 3000000.0f;
    bool probe = true;
    int lightCount = 1024;
    int denseGrid = 24;
    int denseDetail = 8;
    LargeWorldConfig largeWorld;
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
export bool buildTestScene(Engine& engine, const std::string& name, const TestSceneKit& kit, TestSceneInfo& info, const TestSceneParams& params);
export [[nodiscard]] TestSceneKit makeTestSceneKit(Engine& engine, uint32_t floorShader = 0);
export void leaveTestScene(Engine& engine);
export void buildLargeWorldScene(Engine& engine, const LargeWorldConfig& config);
export void largeWorldCameraWorld(Engine& engine, double out[3]);
export void setLargeWorldFlight(Engine& engine, float metersPerSecond);
export void clearTestSceneSystems(Engine& engine);
export void applyTestSceneCamera(Engine& engine, const TestSceneInfo& info);
export [[nodiscard]] double forwardDepthStep(float nearPlane, float farPlane, float distance);
