export module renderer;

import camera;

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

    unsigned int m_shaderProgram;
    unsigned int m_cubeVAO;
    unsigned int m_cubeVBO;
    unsigned int m_cubeEBO;

    int m_modelLoc;
    int m_viewLoc;
    int m_projectionLoc;

    bool m_initialized = false;
};