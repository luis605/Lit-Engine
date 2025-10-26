export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init();
    void update(const SceneDatabase& sceneDatabase, Camera& camera);
    void cleanup();

  private:
    Renderer m_renderer;
};