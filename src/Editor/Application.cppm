module;

#include <GLFW/glfw3.h>
#include <optional>
#include <string>
#include <vector>

export module Editor.application;

import Engine.engine;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.input;
import Engine.mesh;
import Engine.Render.entity;
import Engine.glm;

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
    std::string m_smallObjectThresholdText;
    std::string m_largeObjectThresholdText;
    std::string m_cameraText;

    std::vector<glm::vec3> m_basePositions;
    std::vector<float> m_cosI_x;
    std::vector<float> m_sinI_x;
    std::vector<float> m_cosI_y;
    std::vector<float> m_sinI_y;
    std::vector<float> m_cosI_z;
    std::vector<float> m_sinI_z;
    uint32_t m_movingObjectCount = 0;
    double m_lastAnimMs = 0.0;

    float m_textUpdateTimer = 0.0f;
    float m_nearPlane = 0.1f;
    float m_farPlane = 1000.0f;
    float m_smallObjectThreshold = 0.0f;
    float m_largeObjectThreshold = 0.05f;
    float m_lodBias = 1.0f;
};