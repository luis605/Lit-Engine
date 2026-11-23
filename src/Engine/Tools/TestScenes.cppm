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
    // Full material (metallic, clearcoat, ...); scenes that need more than `material` use it and fall back to `material` / id 0 when unset.
    std::function<uint32_t(const Material&)> materialFull;
};

// Large-world scene (docs/LargeWorld.md): content `originKm` from the world origin, floating origin on, a 4 km flight corridor,
// landmarks at 20 / 50 / 100 km and depth precision probe walls.
export struct LargeWorldConfig {
    double originKm = 500.0;      // world distance of the scene from the origin along +x
    float flySpeed = 0.0f;        // metres per second along +x (the camera is driven in world coordinates; 0 = the user / test drives it)
    int rebaseFrame = 0;          // > 0: force one rebase (origin moves to the camera, snapped) at that tick, camera held still
    double rebaseThreshold = 400.0;
    double rebaseSnap = 256.0;
    bool autoRebase = true;       // floating_origin system on (threshold / snap above)
};

// Knobs of the parameterised scenes. Defaults suit the Editor; LitShot overrides them from its command line.
export struct TestSceneParams {
    float lightIntensity = 3000000.0f;  // showcase, probes
    bool probe = true;                  // probes: place the reflection probe
    int lightCount = 1024;              // light_stress, vsm_mixed
    int denseGrid = 24;                 // dense: grid x grid rocks
    int denseDetail = 8;                // dense / vsm_mixed: icosphere subdivisions of the big rock (small rock uses detail - 1)
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
// Kit backed by the engine: cube / sphere meshes, pooled materials (slots are reused across builds), `floorShader` for ground planes.
export [[nodiscard]] TestSceneKit makeTestSceneKit(Engine& engine, uint32_t floorShader = 0);
// Undo what a built test scene leaves in the engine (testscene.* systems, floating origin, dynamic mesh instances). buildTestScene calls it
// first; call it too before replacing a test scene with something else (after clearing the world).
export void leaveTestScene(Engine& engine);
// Large-world scene entry points (the tests and LitShot use them directly; the registry scene `largeworld` wraps buildLargeWorldScene).
export void buildLargeWorldScene(Engine& engine, const LargeWorldConfig& config);
export void largeWorldCameraWorld(Engine& engine, double out[3]);
export void setLargeWorldFlight(Engine& engine, float metersPerSecond);
export void clearTestSceneSystems(Engine& engine);
export void applyTestSceneCamera(Engine& engine, const TestSceneInfo& info);
export [[nodiscard]] double forwardDepthStep(float nearPlane, float farPlane, float distance);
