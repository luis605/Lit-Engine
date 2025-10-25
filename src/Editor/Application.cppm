module;

#include <GLFW/glfw3.h>

export module Editor.application;

import Engine.engine;
import Engine.camera;
import Engine.scene;
import Engine.input;

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
    Scene m_scene;
};