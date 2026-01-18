module;

#include <cstddef>
#include <vector>
#include <string>
#include <cstdint>

struct GLFWwindow;
struct DiligentData;

export module Engine.renderer;

import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.UI.manager;
import Engine.glm;

export class Renderer {
  public:
    Renderer();
    ~Renderer();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void drawScene(SceneDatabase& sceneDatabase, const Camera& camera);
    void present();
    void cleanup();
    void uploadMesh(const Mesh& mesh);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setSmallObjectThreshold(float threshold);
    void setLargeObjectThreshold(float threshold);
    void setDebugDepthMode(bool enabled);
    bool isDebugDepthMode() const;

  private:
    void createTransformPSO();
    void createHiZPSO();
    void createCullingPSO();
    void createOpaqueSortPSO();
    void createCommandGenPSO();
    void createLargeObjectCullPSO();
    void createLargeObjectSortPSO();
    void createTransparentCullPSO();
    void createTransparentSortPSO();
    void createTransparentCommandGenPSO();
    void createLargeObjectCommandGenPSO();
    void createDepthPrepassPSO();
    void createOpaquePSOs();
    void createTransparentPSO();
    void createDebugDepthPSO();
    void reallocateBuffers(size_t numObjects);

    static constexpr int NUM_FRAMES_IN_FLIGHT = 3;
    size_t m_vboSize = 0;
    size_t m_eboSize = 0;

    int m_maxMipLevel = 0;

    size_t m_numDrawingShaders = 0;

    bool m_initialized = false;
    bool m_meshInfoDirty = true;
    bool fullProfiling;

    UIManager* m_uiManager;

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

    bool m_debugDepthMode = false;

    DiligentData* m_diligent = nullptr;
};