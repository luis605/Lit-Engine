module;

#include <GLFW/glfw3.h>
#include <optional>
#include <string>

export module Editor.application;

import Engine.engine;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.input;
import Engine.mesh;
import Engine.Render.entity;

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
    Entity m_parentEntity;
    std::string m_frameTimeText;
    float m_textUpdateTimer = 0.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
};