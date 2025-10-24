export module engine;

import renderer;
import camera;
export class Engine {
  public:
    Engine();
    ~Engine();

    void init();
    void update(Camera& camera);
    void cleanup();

  private:
    Renderer m_renderer;
};