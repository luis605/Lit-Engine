module;

#include <cstddef>

export module Engine.renderer;

import Engine.shader;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.Render.shaderManager;
import Engine.mesh;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init();
    void drawScene(SceneDatabase& sceneDatabase, const Camera& camera);
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
    unsigned int m_renderableBuffer = 0;

    size_t m_vboSize = 0;
    size_t m_eboSize = 0;

    ShaderManager m_shaderManager;
    Shader* m_cullingShader;
    Shader* m_transparentCullShader;
    Shader* m_bitonicSortShader;
    Shader* m_transparentCommandGenShader;

    unsigned int m_visibleTransparentObjectBuffer = 0;
    unsigned int m_transparentAtomicCounter = 0;
    unsigned int m_transparentDrawCommandBuffer = 0;

    size_t m_numDrawingShaders = 0;

    bool m_initialized = false;
};