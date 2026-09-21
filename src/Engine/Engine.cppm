module;

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
import Engine.inputactions;
import Engine.glm;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void tick(float deltaTime);
    void debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f));
    void debugBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color = glm::vec4(1.0f));
    void debugSphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void debugRay(const Ray& ray, float length, const glm::vec4& color = glm::vec4(1.0f));
    void debugHierarchy(const glm::vec4& color = glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
    [[nodiscard]] std::optional<RayHit> pick(float screenX, float screenY);
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
    void applyWorldAnimation();

    Renderer m_renderer;
    World m_world;
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    InputActions m_input;
    std::vector<glm::vec3> m_animationBase;
    uint32_t m_animationOffset = 0;
    uint32_t m_animationCount = 0;
    float m_fixedStep = 1.0f / 60.0f;
    float m_accumulator = 0.0f;
    std::unordered_map<std::string, uint32_t> m_meshIds;
    std::unordered_map<uint32_t, std::string> m_meshNames;
};