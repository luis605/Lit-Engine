export module renderer;

import shader;
import camera;
import std;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init();
    void drawScene(const Camera& camera);
    void cleanup();

  private:
    void setupShaders();
    void setupCubeMesh();

    std::unique_ptr<Shader> m_shader;

    unsigned int m_cubeVAO;
    unsigned int m_cubeVBO;
    unsigned int m_cubeEBO;

    bool m_initialized = false;
};