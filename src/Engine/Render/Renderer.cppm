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
import Engine.Render.entity;
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
    uint32_t uploadMesh(const Mesh& mesh);
    glm::vec4 getMeshBounds(uint32_t meshId) const;
    void addDebugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color);
    void uploadBasePositions(const std::vector<glm::vec3>& basePositions);
    void setAnimation(float time, uint32_t movingCount, uint32_t entityOffset);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setLodBias(float bias);
    void setForcedLod(int lod);
    int getForcedLod() const;
    void setSmallObjectThreshold(float threshold);
    void setLargeObjectThreshold(float threshold);
    void setDebugDepthMode(bool enabled);
    bool isDebugDepthMode() const;
    void setFullProfiling(bool enabled);

   private:
    void createTransformPSO();
    void createAnimPSO();
    void createHiZPSO();
    void createCullingPSO();
    void createCommandGenPSO();
    void createPrefixSumPSO();
    void createScatterPSO();
    void createDispatchArgsPSO();
    void createApplyDirtyPSO();
    void createMarkTouchedPSO();
    void createLargeObjectCullPSO();
    void createTransparentCullPSO();
    void createTransparentCommandGenPSO();
    void createLargeObjectCommandGenPSO();
    void createDepthPrepassPSO();
    void createOpaquePSOs();
    void createTransparentPSO();
    void createDebugDepthPSO();
    void createDebugLinePSO();
    void reallocateBuffers(size_t numObjects);
    uint32_t uploadMeshSlot(const Mesh& mesh);

    static constexpr int NUM_FRAMES_IN_FLIGHT = 3;
    size_t m_vboSize = 0;
    size_t m_eboSize = 0;

    int m_maxMipLevel = 0;

    size_t m_numDrawingShaders = 0;
    std::vector<unsigned int> m_bucketZeros;

    std::vector<uint8_t> m_levelNeedsProcessing;

    std::vector<Entity> m_pendingDirty[NUM_FRAMES_IN_FLIGHT];
    std::vector<Entity> m_dirtyIndexScratch;
    std::vector<glm::mat4> m_dirtyPayloadScratch;
    uint32_t m_transformEpoch = 0;

    float m_animTime = 0.0f;
    uint32_t m_animMovingCount = 0;
    uint32_t m_animEntityOffset = 0;

    bool m_initialized = false;
    bool m_meshInfoDirty = true;
    bool fullProfiling;

    UIManager* m_uiManager = nullptr;

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
    std::vector<uint32_t> m_transparentIds;
    uint32_t m_transparentIdCounts[NUM_FRAMES_IN_FLIGHT] = {};
    bool m_transparentIdsStale = true;
    uint64_t m_processedTransformVersion = 0;
    int m_hierarchyUpdateCounter = 0;
    int m_fullTransformUpdateCounter = 0;
    int m_transformUpdateCounter = 0;
    int m_renderableUpdateCounter = 0;

    float m_lodBias = 1.0f;
    int m_forcedLod = -1;
    float m_smallObjectThreshold = 0.0f;
    float m_largeObjectThreshold = 0.1f;
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    bool m_renderPrePass = true;
    float m_prePassToggleTimer = 0.0f;
    float m_lastFrameTime = 0.0f;

    bool m_debugDepthMode = false;
    std::vector<float> m_debugLines;

    DiligentData* m_diligent = nullptr;
};