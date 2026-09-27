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
import Engine.Render.material;
import Engine.glm;

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
    [[nodiscard]] int width() const { return m_windowWidth; }
    [[nodiscard]] int height() const { return m_windowHeight; }
    uint32_t uploadMesh(const Mesh& mesh);
    glm::vec4 getMeshBounds(uint32_t meshId) const;
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
    bool enableImGui();
    void beginUi();
    [[nodiscard]] bool imguiEnabled() const { return m_imgui && m_imgui->initialized(); }

   private:
    void createScreenTargets();
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
    void rebindSurfaceResources();
    void uploadCascadeCull();
    void drawShadows(uint32_t objectCount);
    void drawLocalShadows(uint32_t objectCount);
    void ensureShadowHiZ();
    void buildShadowHiZ(int cascade);
    void bindBackBuffer();
    void drawSurfaces(int phaseIndex, int cascade, uint32_t view = 0);
    void createDebugDepthPSO();
    void createDebugLinePSO();
    void createPipelines();
    void uploadAllRenderables(const SceneDatabase& sceneDatabase);
    void uploadDirtyRenderables(SceneDatabase& sceneDatabase);
    void uploadDirtyHierarchies(SceneDatabase& sceneDatabase);
    [[nodiscard]] bool pipelinesReady() const;
    void cullVisibilityPhase(int phase, uint32_t objectCount, int hizFrame);
    void buildVisibilityDraws(int phase, uint32_t views = 1);
    void drawVisibilityPhase(int phase);
    void bindSceneTargets(bool clear);
    void buildHiZ();
    void drawTransparency();
    void drawProfilingHud();
    void drawOverlays(const Camera& camera);
    void captureBackBuffer();
    std::string m_screenshotPath;
    bool reallocateBuffers(size_t numObjects);
    void releaseObjectBuffers();
    [[nodiscard]] size_t objectBudget() const;
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

    float m_lastFrameTime = 0.0f;

    bool m_debugDepthMode = false;
    std::vector<float> m_debugLines;
    std::vector<float> m_debugOverlayLines;
    uint32_t m_frameCounter = 0;
    float m_lastDeltaTime = 0.016f;
    Lighting m_lighting;
    CustomPasses m_custom;
    CustomFrame customFrame() const;
    DiligentData* m_diligent = nullptr;
};