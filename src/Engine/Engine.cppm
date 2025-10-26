module;

#include <optional>

export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;

export class Engine {
  public:
    Engine();
    ~Engine();

    void init();
    void update(const SceneDatabase& sceneDatabase, Camera& camera, const std::optional<Mesh>& mesh);
    void cleanup();

  private:
    Renderer m_renderer;
};