export module Engine.renderer;

import Engine.shader;
import Engine.camera;
import Engine.Render.scenedatabase;
import std;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init();
    void drawScene(const SceneDatabase& sceneDatabase, const Camera& camera);
    void cleanup();

  private:
    void setupShaders();

    std::unique_ptr<Shader> m_shader;

    bool m_initialized = false;
};