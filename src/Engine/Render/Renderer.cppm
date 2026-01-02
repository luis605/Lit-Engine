module;

#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>

struct GLFWwindow;
struct DiligentData;

export module Engine.renderer;

import Engine.shader;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.Render.shaderManager;
import Engine.mesh;
import Engine.UI.manager;
import Engine.glm;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void drawScene(SceneDatabase& sceneDatabase, const Camera& camera);
    void cleanup();
    void uploadMesh(const Mesh& mesh);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setSmallObjectThreshold(float threshold);
    void setLargeObjectThreshold(float threshold);

  private:
    void setupShaders();
    void createTransformPSO();
    void createHiZPSO();
    void createCullingPSO();
    void createOpaqueSortPSO();
    void createCommandGenPSO();
    void createLargeObjectCullPSO();
    void reallocateBuffers(size_t numObjects);

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;

    unsigned int m_drawCommandBuffer = 0;
    unsigned int m_objectBuffer = 0;
    unsigned int m_hierarchyBuffer = 0;
    unsigned int m_visibleObjectAtomicCounter = 0;
    unsigned int m_drawAtomicCounterBuffer = 0;
    unsigned int m_renderableBuffer = 0;
    unsigned int m_sortedHierarchyBuffer = 0;

    static constexpr int NUM_FRAMES_IN_FLIGHT = 3;
    size_t m_vboSize = 0;
    size_t m_eboSize = 0;

    ShaderManager m_shaderManager;
    Shader* m_cullingShader;
    Shader* m_transparentCullShader;
    Shader* m_bitonicSortShader;
    Shader* m_opaqueSortShader;
    Shader* m_transparentCommandGenShader;
    Shader* m_commandGenShader;

    unsigned int m_visibleObjectBuffer = 0;
    unsigned int m_visibleTransparentObjectIdsBuffer = 0;
    unsigned int m_transparentAtomicCounter = 0;
    unsigned int m_transparentDrawCommandBuffer = 0;

    unsigned int m_depthFbo[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_depthRenderbuffer[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_depthRbo = 0;
    unsigned int m_hizFbo = 0;
    unsigned int m_hizTexture[NUM_FRAMES_IN_FLIGHT] = {0};

    Shader* m_largeObjectCullShader = nullptr;
    Shader* m_largeObjectSortShader = nullptr;
    Shader* m_largeObjectCommandGenShader = nullptr;
    Shader* m_depthPrepassShader = nullptr;
    unsigned int m_depthPrepassAtomicCounter = 0;
    unsigned int m_depthPrepassDrawCommandBuffer = 0;
    unsigned int m_visibleLargeObjectBuffer = 0;
    unsigned int m_visibleLargeObjectAtomicCounter = 0;

    Shader* m_hizMipmapShader = nullptr;
    int m_maxMipLevel = 0;

    size_t m_numDrawingShaders = 0;

    bool m_initialized = false;
    bool m_meshInfoDirty = true;
    bool fullProfiling;

    UIManager* m_uiManager;

    unsigned int m_queryStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransformStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransformEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryCullStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryCullEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryOpaqueSortStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryOpaqueSortEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryCommandGenStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryCommandGenEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryLargeObjectSortStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryLargeObjectSortEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryLargeObjectCommandGenStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryLargeObjectCommandGenEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryDepthPrePassStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryDepthPrePassEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentCullStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentCullEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentSortStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentSortEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentCommandGenStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentCommandGenEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryOpaqueDrawStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryOpaqueDrawEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentDrawStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryTransparentDrawEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryHizMipmapStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryHizMipmapEnd[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryUiStart[NUM_FRAMES_IN_FLIGHT] = {0};
    unsigned int m_queryUiEnd[NUM_FRAMES_IN_FLIGHT] = {0};

    unsigned int m_currentFrame = 0;

    size_t m_objectBufferSize = 0;
    size_t m_hierarchyBufferSize = 0;
    size_t m_renderableBufferSize = 0;
    size_t m_sortedHierarchyBufferSize = 0;
    size_t m_drawCommandBufferSize = 0;
    size_t m_visibleObjectBufferSize = 0;
    size_t m_visibleTransparentObjectIdsBufferSize = 0;
    size_t m_transparentDrawCommandBufferSize = 0;
    size_t m_depthPrepassDrawCommandBufferSize = 0;
    size_t m_visibleLargeObjectBufferSize = 0;
    size_t m_sceneUBOSize = 0;
    size_t m_maxObjects = 0;

    uint64_t m_processedHierarchyVersion = 0;
    uint64_t m_processedDataVersion = 0;
    int m_hierarchyUpdateCounter = 0;
    int m_dataUpdateCounter = 0;

    float m_smallObjectThreshold = 0.005f;
    float m_largeObjectThreshold = 0.1f;
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    bool m_renderPrePass = true;
    float m_prePassToggleTimer = 0.0f;
    float m_lastFrameTime = 0.0f;

    DiligentData* m_diligent = nullptr;
};