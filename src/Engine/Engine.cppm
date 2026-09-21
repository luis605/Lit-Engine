module;

#include <optional>
#include <string>
#include <vector>
#include <cstdint>

struct GLFWwindow;

export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.World;
import Engine.glm;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void update();
    void update(World& world);
    [[nodiscard]] World& world() { return m_world; }
    [[nodiscard]] Camera& camera() { return m_world.camera(); }
    void present();
    void cleanup();
    uint32_t uploadMesh(const Mesh& mesh);
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
    Renderer m_renderer;
    World m_world;
};