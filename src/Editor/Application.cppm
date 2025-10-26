module;

#include <GLFW/glfw3.h>
#include <optional>

export module Editor.application;

import Engine.engine;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.input;
import Engine.mesh;

export class Application {
  public:
    Application();
    ~Application();
    void update();

    bool isRunning() const;

  private:
    void processInput(float deltaTime);

    GLFWwindow* m_window;
    Engine m_engine;
    Camera camera;
    SceneDatabase m_sceneDatabase;
    std::optional<Mesh> m_mesh;
};