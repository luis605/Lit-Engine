module;

#include <optional>
#include <string>

struct GLFWwindow;

export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.glm;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void cleanup();
    void uploadMesh(const Mesh& mesh);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setSmallObjectThreshold(float threshold);
    void setLargeObjectThreshold(float threshold);

  private:
    Renderer m_renderer;
};