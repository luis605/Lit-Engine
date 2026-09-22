module;

#include <array>
#include <cstddef>
#include <span>
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

export struct GpuEntityState {
    glm::mat4 world;
    glm::vec4 cullSphere;
};

export class Renderer {
   public:
    Renderer();
    ~Renderer();

    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void drawScene(SceneDatabase& sceneDatabase, const Camera& camera);
    void present();
    void cleanup();
    bool resizeToWindow();
    [[nodiscard]] int width() const { return m_windowWidth; }
    [[nodiscard]] int height() const { return m_windowHeight; }
    uint32_t uploadMesh(const Mesh& mesh);
    glm::vec4 getMeshBounds(uint32_t meshId) const;
    void setLights(bool enabled, const std::array<glm::vec4, 2>& directional, const std::vector<glm::vec4>& packed);
    void reloadShaders();
    void setMaterial(uint32_t index, const glm::vec4& colorAndStrength);
    void addDebugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool overlay = false);
    void uploadBasePositions(const std::vector<glm::vec3>& basePositions);
    void setAnimation(float time, uint32_t movingCount, uint32_t entityOffset);
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setLodBias(float bias);
    void setForcedLod(int lod);
    int getForcedLod() const;
    void setSmallObjectThreshold(float threshold);
    void setDebugDepthMode(bool enabled);
    bool isDebugDepthMode() const;
    void setFullProfiling(bool enabled);
    [[nodiscard]] std::vector<GpuEntityState> readGpuEntityStates(size_t count);

   private:
    void createScreenTargets();
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
    void createTransparentCullPSO();
    void createTransparentCommandGenPSO();
    void createOpaquePSOs();
    void createTransparentPSO();
    void createDebugDepthPSO();
    void createDebugLinePSO();
    void createPipelines();
    void uploadTransparentIds(const SceneDatabase& sceneDatabase);
    void uploadAllRenderables(const SceneDatabase& sceneDatabase);
    void uploadDirtyRenderables(SceneDatabase& sceneDatabase);
    void uploadDirtyHierarchies(SceneDatabase& sceneDatabase);
    [[nodiscard]] bool pipelinesReady() const;
    void cullVisibilityPhase(int phase, uint32_t objectCount, int hizFrame);
    void buildVisibilityDraws(int phase);
    void drawVisibilityPhase(int phase);
    void bindSceneTargets(bool clear);
    void buildHiZ();
    void drawProfilingHud();
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
    std::vector<Entity> m_transformWorkScratch;
    std::vector<Entity> m_transformWork;
    std::vector<uint32_t> m_transformWorkLevelCounts;
    std::vector<uint32_t> m_transformWorkLevelHeads;
    std::vector<uint32_t> m_transformWorkStamp;
    uint32_t m_transformWorkEpoch = 0;
    std::array<uint64_t, 3> m_animLeafCheckKey{};
    bool m_animatedEntitiesAreLeaves = false;
    bool animatedEntitiesAreLeaves(const SceneDatabase& sceneDatabase);
    bool collectTransformWork(const SceneDatabase& sceneDatabase, std::span<const Entity> dirty);
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

    size_t m_maxObjects = 0;

    uint64_t m_processedHierarchyVersion = 0;
    uint64_t m_processedStructureVersion = 0;
    uint64_t m_processedDataVersion = 0;
    std::vector<uint32_t> m_transparentIds;
    std::vector<uint32_t> m_packedRenderables;
    uint32_t m_transparentIdCount = 0;
    uint64_t m_processedTransformVersion = 0;
    int m_hierarchyUpdateCounter = 0;
    int m_fullTransformUpdateCounter = 0;
    int m_transformUpdateCounter = 0;
    int m_renderableUpdateCounter = 0;

    float m_lodBias = 1.0f;
    int m_forcedLod = -1;
    float m_smallObjectThreshold = 0.0f;
    GLFWwindow* m_window = nullptr;
    int m_windowWidth = 0;
    int m_windowHeight = 0;

    float m_lastFrameTime = 0.0f;

    bool m_debugDepthMode = false;
    std::vector<float> m_debugLines;
    std::vector<float> m_debugOverlayLines;
    bool m_lightOverride = false;
    std::array<glm::vec4, 2> m_directional{};
    std::vector<glm::vec4> m_lightData;
    std::vector<glm::vec4> m_defaultLightData = {
        glm::vec4(-80.0f, 50.0f, -50.0f, 250.0f), glm::vec4(0.1f, 0.75f, 1.0f, 2.5f), glm::vec4(0.0f), glm::vec4(0.0f),
        glm::vec4(80.0f, -30.0f, 50.0f, 250.0f), glm::vec4(1.0f, 0.45f, 0.15f, 2.5f), glm::vec4(0.0f), glm::vec4(0.0f)};
    bool m_lightDataDirty = true;
    bool m_uploadedLightSource = false;

    DiligentData* m_diligent = nullptr;
};