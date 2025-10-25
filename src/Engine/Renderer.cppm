export module renderer;

import shader;
import camera;
import std;
import scene;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init();
    void drawScene(const Scene& scene, const Camera& camera);
    void cleanup();

  private:
    void setupShaders();

    std::unique_ptr<Shader> m_shader;

    bool m_initialized = false;
};