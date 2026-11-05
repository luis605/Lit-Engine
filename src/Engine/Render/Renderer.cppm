module;

#include <array>
#include <cstddef>
#include <span>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

struct GLFWwindow;
struct DiligentData;
struct PortalHistory;
namespace Diligent {
struct ITexture;
struct IBuffer;
struct IPipelineState;
struct IShaderResourceBinding;
}

export module Engine.renderer;

import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.Render.entity;
import Engine.mesh;
import Engine.UI.manager;
import Engine.UI.imgui;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.governor;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;
import Engine.glm;

enum class GpuPass : uint8_t { Upload, Transform, Sky, Shadows, Cull, CommandGen, OpaqueDraw, HiZ, RetestCull, RetestCommandGen, RetestDraw, TransparentCull, TransparentCommandGen, AmbientOcclusion, Reflections, Lighting, TransparentDraw, Post, Ui, Count };

export struct GpuEntityState {
    glm::mat4 world;
    glm::vec4 cullSphere;
};

export struct GpuPassTiming {
    std::string name;
    double milliseconds = 0.0;
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
    [[nodiscard]] int renderWidth() const { return m_renderWidth; }
    [[nodiscard]] int renderHeight() const { return m_renderHeight; }
    [[nodiscard]] const QualityGovernor& governor() const { return m_governor; }
    [[nodiscard]] int width() const { return m_windowWidth; }
    [[nodiscard]] int height() const { return m_windowHeight; }
    uint32_t uploadMesh(const Mesh& mesh);
    uint32_t uploadProcessedMesh(const Mesh& optimizedMesh, const std::vector<Mesh>& lodChain);
    glm::vec4 getMeshBounds(uint32_t meshId) const;
    void setReflectionScene(const ReflectionScene& scene);
    void bakeProbes();
    void setLights(bool enabled, const std::array<glm::vec4, 2>& directional, const std::vector<glm::vec4>& packed);
    void reloadShaders();
    void setMaterial(uint32_t index, const Material& material);
    [[nodiscard]] RenderSettings& settings() { return m_lighting.settings(); }
    [[nodiscard]] bool initialized() const { return m_initialized; }
    [[nodiscard]] size_t overCapacity() const { return m_failedCapacity; }
    [[nodiscard]] float currentEv100() const { return m_lighting.currentEv100(); }
    [[nodiscard]] glm::vec3 sunDirection() const { return m_lighting.sunDirection(); }
    int loadTexture(const std::string& path, bool srgb) { return m_lighting.loadTexture(path, srgb); }
    uint32_t loadSurfaceShader(const std::string& name, const std::string& path);
    [[nodiscard]] CustomPasses& custom() { return m_custom; }
    [[nodiscard]] std::vector<std::string> surfaceNames() const;
    [[nodiscard]] std::string surfacePath(uint32_t id) const;
    [[nodiscard]] std::string surfaceStatus(const std::string& name) const;
    [[nodiscard]] std::vector<ShaderError> surfaceErrors() const;
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
    [[nodiscard]] std::vector<GpuPassTiming> gpuTimings() const;
    [[nodiscard]] double gpuTotalMilliseconds() const;
    void setProfilingHud(bool enabled) { m_profilingHud = enabled; }
    void requestScreenshot(std::string path) { m_screenshotPath = std::move(path); }
    void setPortalFrame(PortalFrame frame);
    void portalCrossed() {
        m_lighting.snapExposure();
        m_lighting.setPyramidValid(false);
    }
    void setObservationQuery(ObservationQuery query);
    void clearObservationQuery();
    bool takeObservation(ObservationReadback& out);
    [[nodiscard]] const std::vector<PortalViewStats>& portalStats() const { return m_portalStats; }
    bool enableImGui();
    void beginUi();
    [[nodiscard]] bool imguiEnabled() const { return m_imgui && m_imgui->initialized(); }

   private:
    struct TransformPlan {
        bool hierarchyChanged = false;
        bool bulkUpload = false;
        bool fullUpdate = false;
        bool localMatricesUploading = false;
        bool forceAllLevels = false;
        bool sparse = false;
    };

    void createDevice();
    void createTimestampQueries();
    void createGeometryBuffers();
    void createFences();
    void createScreenTargets();
    void updateQualityScale(float deltaTime);
    bool applyRenderSize();
    void createTransformPSO();
    void createHiZPSO();
    void createCullingPSO();
    void createCommandGenPSO();
    void createPrefixSumPSO();
    void createScatterPSO();
    void createDispatchArgsPSO();
    void createApplyDirtyPSO();
    void createTransparentCullPSO();
    void createTransparentPSO();
    void createSurfacePipelines();
    void createDebugDepthPSO();
    void createDepthUpscalePSO();
    void resolveOverlayDepth();
    void createDebugLinePSO();
    void createPortalPSO();
    void createPortalCompositePSO();
    void createPipelines();
    [[nodiscard]] bool pipelinesReady() const;

    bool reallocateBuffers(size_t numObjects);
    void releaseObjectBuffers();
    void releaseBindings();
    void allocateMeshBuffers();
    void allocateVisibilityBuffers();
    void allocateShadowBuffers();
    void allocateCandidateBuffers();
    void allocateObjectBuffers();
    [[nodiscard]] bool objectBuffersAllocated() const;
    [[nodiscard]] size_t objectBudget() const;
    void ensureCapacity(uint32_t objectCount);
    void bindComputeResources(int frameIndex);
    void bindDispatchArgsResources(int frameIndex);
    void bindVisibilityResources(int frameIndex);
    void bindShadowResources(int frameIndex);
    void rebindSurfaceResources();

    uint32_t uploadMeshSlot(const Mesh& mesh);
    void growGeometryBuffers(size_t vertexDataSize, size_t indexDataSize);
    void storeNormalSamples(uint32_t baseId, const Mesh& mesh);

    void beginFrame(float deltaTime);
    void collectTimings(float deltaTime);
    void pollObservation();
    void beginPass(GpuPass pass);
    void endPass(GpuPass pass);
    void signalFrameFence();
    void drawEmptyFrame(const Camera& camera);
    FrameInput makeFrameInput(const Camera& camera, float currentFrameTime, float deltaTime);
    void renderPortalPrepass(const SceneUniforms& sceneUniforms, uint32_t objectCount);
    void renderOpaqueGeometry(uint32_t objectCount, bool portalsActive);
    void renderShadowsAndTransparentCull(uint32_t objectCount);
    void shadeScene(bool portalsActive);
    void renderTransparentAndCustomEffects();
    void finishFrame(const FrameInput& frameInput, const Camera& camera);

    void syncVersions(const SceneDatabase& sceneDatabase);
    TransformPlan uploadSceneData(SceneDatabase& sceneDatabase);
    void mergeTransformDirty(SceneDatabase& sceneDatabase, TransformPlan& plan);
    void ensureLevelOrder(SceneDatabase& sceneDatabase);
    void uploadHierarchies(SceneDatabase& sceneDatabase);
    void uploadTransforms(const SceneDatabase& sceneDatabase);
    void uploadRenderables(SceneDatabase& sceneDatabase);
    void uploadAllRenderables(const SceneDatabase& sceneDatabase);
    void uploadDirtyRenderables(SceneDatabase& sceneDatabase);
    void uploadDirtyHierarchies(SceneDatabase& sceneDatabase);
    void uploadMeshInfo();
    void planTransformWork(SceneDatabase& sceneDatabase, TransformPlan& plan);
    void markDirtyLevels(const SceneDatabase& sceneDatabase);
    void applyDirtyTransforms(const SceneDatabase& sceneDatabase);
    void dispatchTransforms(const SceneDatabase& sceneDatabase, const TransformPlan& plan);
    void dispatchTransformGroup(Diligent::IShaderResourceBinding* binding, uint32_t listOffset, uint32_t count, bool forceRecompute, bool directEntityRange);
    [[nodiscard]] bool animationActive() const;
    bool animatedEntitiesAreLeaves(const SceneDatabase& sceneDatabase);
    bool collectTransformWork(const SceneDatabase& sceneDatabase, std::span<const Entity> dirty);

    void cullVisibilityPhase(int phase, uint32_t objectCount, int hizFrame);
    void cullVisibilityPhase(int phase, uint32_t objectCount, Diligent::ITexture* hiz);
    void resetVisibilityCounters(int phaseIndex);
    void prepareCandidateDispatch(int phaseIndex, uint32_t objectCount);
    void buildVisibilityDraws(int phase, uint32_t views = 1);
    void drawVisibilityPhase(int phase);
    void drawSurfaces(int phaseIndex, int cascade, uint32_t view = 0);
    void bindSceneTargets(bool clear);
    void bindBackBuffer(bool sceneDepthValid = true);
    void buildHiZ();
    void drawTransparency();

    void uploadCascadeCull(bool empty = false);
    void drawShadows(uint32_t objectCount);
    void drawLocalShadows(uint32_t objectCount);
    void renderCascade(int cascadeIndex);
    void dispatchCascadeCull(int cascadeIndex, int hizLayer, Diligent::IPipelineState* pipeline, Diligent::IShaderResourceBinding* binding, Diligent::IBuffer* dispatchArgs, const glm::mat4& hizViewProjection, bool hizValid);
    void ensureShadowHiZ();
    void releaseShadowHiZ();
    void createShadowHiZArgs(uint32_t size, uint32_t mips);
    void createShadowHiZLayer(int layer, Diligent::ITexture* shadowMap, uint32_t size, uint32_t mips);
    void buildShadowHiZ(int cascade);

    void renderPortalViews(const SceneUniforms& base, SceneUniforms& restore, uint32_t objectCount);
    void renderPortalNode(int index, const SceneUniforms& base, uint32_t objectCount);
    void renderPortalGeometry(int index, uint32_t objectCount, PortalHistory& history);
    void shadePortalView(int index);
    void compositePortals(int index);
    void resolvePortalDepth(int index);
    void maskPortalView(int index);
    bool ensurePortalScratch();
    void recordPortalStats(int index);
    void readPortalStats();

    void drawProfilingHud();
    void drawOverlays(const Camera& camera);
    void drawDebugDepth(const Camera& camera);
    void drawDebugLines(const Camera& camera);
    void captureBackBuffer();
    CustomFrame customFrame() const;

    static constexpr int NUM_FRAMES_IN_FLIGHT = 3;

    std::string m_screenshotPath;
    PortalFrame m_portalFrame;
    ObservationQuery m_obsQuery;
    bool m_obsActive = false;
    std::vector<ObservationReadback> m_obsReady;
    std::vector<PortalViewStats> m_portalStats;

    size_t m_vboSize = 0;
    size_t m_eboSize = 0;

    int m_maxMipLevel = 0;

    size_t m_numDrawingShaders = 0;
    std::vector<unsigned int> m_bucketZeros;

    std::vector<uint8_t> m_levelNeedsProcessing;

    std::vector<Entity> m_pendingDirty;
    std::vector<Entity> m_dirtyIndexScratch;
    std::vector<Entity> m_transformWorkScratch;
    std::vector<Entity> m_transformWork;
    std::vector<uint32_t> m_transformWorkLevelCounts;
    std::vector<uint32_t> m_transformWorkLevelHeads;
    std::vector<uint32_t> m_transformWorkStamp;
    uint32_t m_transformWorkEpoch = 0;
    std::array<uint64_t, 3> m_animLeafCheckKey{};
    bool m_animatedEntitiesAreLeaves = false;
    std::vector<glm::mat4> m_dirtyPayloadScratch;
    uint32_t m_transformEpoch = 0;

    float m_animTime = 0.0f;
    uint32_t m_animMovingCount = 0;
    uint32_t m_animEntityOffset = 0;

    bool m_initialized = false;
    bool m_meshInfoDirty = true;
    bool m_fullProfiling = false;
    bool m_profilingHud = true;

    UIManager* m_uiManager = nullptr;
    ImGuiLayer* m_imgui = nullptr;

    unsigned int m_currentFrame = 0;

    size_t m_maxObjects = 0;
    size_t m_failedCapacity = 0;

    uint64_t m_processedHierarchyVersion = 0;
    uint64_t m_processedStructureVersion = 0;
    uint64_t m_processedDataVersion = 0;
    std::vector<uint32_t> m_packedRenderables;
    std::vector<uint8_t> m_packedAlphas;
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
    int m_renderWidth = 0;
    int m_renderHeight = 0;
    QualityGovernor m_governor;

    float m_lastFrameTime = 0.0f;

    bool m_debugDepthMode = false;
    std::vector<float> m_debugLines;
    std::vector<float> m_debugOverlayLines;
    uint32_t m_frameCounter = 0;
    float m_lastDeltaTime = 0.016f;
    Lighting m_lighting;
    CustomPasses m_custom;
    DiligentData* m_diligent = nullptr;
};
