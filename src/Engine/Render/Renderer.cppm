module;

#include <memory>
#include <optional>
#include <vector>

export module Engine.renderer;

import Engine.shader;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init();
    void drawScene(const SceneDatabase& sceneDatabase, const Camera& camera);
    void cleanup();
    void uploadMesh(const Mesh& mesh);

  private:
    void setupShaders();

    struct GPUMesh;
    std::vector<GPUMesh> m_gpuMeshes;

    std::unique_ptr<Shader> m_shader;

    bool m_initialized = false;
};