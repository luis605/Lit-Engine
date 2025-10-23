export module application;

extern "C" {
    #include "GLFW/glfw3.h"
}
export class Application {
public:
    Application();
    ~Application();
    void update();

    bool isRunning() const;

private:
    GLFWwindow* m_window;
};