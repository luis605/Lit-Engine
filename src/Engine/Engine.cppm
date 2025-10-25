export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.scene;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init();
    void update(const Scene& scene, Camera& camera);
    void cleanup();

  private:
    Renderer m_renderer;
};