module;

#include <GLFW/glfw3.h>

export module application;

import engine;
import camera;
import scene;

export class Application {
  public:
    Application();
    ~Application();
    void update();

    bool isRunning() const;

  private:
    GLFWwindow* m_window;
    Engine m_engine;
    Camera camera;
    Scene m_scene;
};