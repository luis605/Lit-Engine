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

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;

    unsigned int m_drawCommandBuffer = 0;
    unsigned int m_objectBuffer = 0;
    unsigned int m_atomicCounterBuffer = 0;
    unsigned int m_meshInfoBuffer = 0;

    std::unique_ptr<Shader> m_shader;
    std::unique_ptr<Shader> m_cullingShader;

    bool m_initialized = false;
};