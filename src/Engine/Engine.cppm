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
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void cleanup();
    void uploadMesh(const Mesh& mesh);

  private:
    Renderer m_renderer;
};