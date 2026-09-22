module;

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct GLFWwindow;

export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.Physics;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.inputactions;
import Engine.glm;

export enum class Phase { PreUpdate, FixedUpdate, Update, PostUpdate };

export using SystemFn = std::function<void(World&, float)>;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void tick(float deltaTime);
    void reloadShaders();
    void setShaderWatch(bool enabled) { m_shaderWatch = enabled; }
    [[nodiscard]] const TimeState& time() const { return m_world.time(); }
    [[nodiscard]] PhysicsSettings& physics() { return m_physics; }
    [[nodiscard]] AnimationLibrary& animations() { return m_animations; }
    [[nodiscard]] JobSystem& jobs() { return m_jobs; }
    [[nodiscard]] Profiler& profiler() { return m_profiler; }
    void addSystem(Phase phase, std::string name, SystemFn fn, bool runWhenPaused = false);
    bool removeSystem(const std::string& name);
    bool setSystemEnabled(const std::string& name, bool enabled);
    [[nodiscard]] bool isSystemEnabled(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> systemNames(Phase phase) const;
    void setTimeScale(float scale) { m_world.timeState().timeScale = scale < 0.0f ? 0.0f : scale; }
    void setPaused(bool paused) { m_world.timeState().paused = paused; }
    void debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f), bool overlay = false);
    void debugBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color = glm::vec4(1.0f), bool overlay = false);
    void debugSphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void debugRay(const Ray& ray, float length, const glm::vec4& color = glm::vec4(1.0f));
    void debugHierarchy(const glm::vec4& color = glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
    [[nodiscard]] std::optional<RayHit> pick(float screenX, float screenY);
    [[nodiscard]] Ray screenRay(float screenX, float screenY);
    [[nodiscard]] std::optional<glm::vec2> worldToScreen(const glm::vec3& point);
    [[nodiscard]] glm::vec2 windowSize() const { return glm::vec2(static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)); }
    void setFixedTimestep(float seconds) { m_fixedStep = seconds; }
    void update();
    void update(World& world);
    [[nodiscard]] World& world() { return m_world; }
    [[nodiscard]] InputActions& input() { return m_input; }
    [[nodiscard]] Camera& camera() { return m_world.camera(); }
    void present();
    void cleanup();
    uint32_t uploadMesh(const Mesh& mesh);
    uint32_t loadMesh(const std::string& name);
    uint32_t createMaterial(const glm::vec3& color, float strength = 1.0f);
    void updateMaterial(uint32_t id, const glm::vec3& color, float strength = 1.0f);
    void uploadBasePositions(const std::vector<glm::vec3>& basePositions);
    void setAnimation(float time, uint32_t movingCount, uint32_t entityOffset);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setLodBias(float bias);
    void setForcedLod(int lod);
    int getForcedLod() const;
    void setSmallObjectThreshold(float threshold);
    void setLargeObjectThreshold(float threshold);
    void setDebugDepthMode(bool enabled);
    bool isDebugDepthMode() const;
    void setFullProfiling(bool enabled);

  private:
    struct System {
        Phase phase;
        std::string name;
        SystemFn fn;
        bool enabled = true;
        bool runWhenPaused = false;
    };

    void runSystems(Phase phase, float dt, bool paused);
    void applyWorldAnimation();

    Renderer m_renderer;
    JobSystem m_jobs;
    Profiler m_profiler;
    World m_world;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    InputActions m_input;
    std::vector<glm::vec3> m_animationBase;
    uint32_t m_animationOffset = 0;
    uint32_t m_animationCount = 0;
    uint32_t m_materialCount = 1;
    bool m_shaderWatch = false;
    std::chrono::steady_clock::time_point m_lastShaderPoll{};
    std::unordered_map<std::string, long long> m_shaderStamps;
    bool shadersChanged();
    PhysicsSettings m_physics;
    AnimationLibrary m_animations;
    std::vector<System> m_systems;
    float m_fixedStep = 1.0f / 60.0f;
    float m_accumulator = 0.0f;
    std::unordered_map<std::string, uint32_t> m_meshIds;
    std::unordered_map<uint32_t, std::string> m_meshNames;
};