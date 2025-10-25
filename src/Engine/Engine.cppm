export module engine;

import renderer;
import camera;
import scene;

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