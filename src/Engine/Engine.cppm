module;

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

struct GLFWwindow;

export module Engine.engine;

import Engine.renderer;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.mesh;
import Engine.asset;
import Engine.Skin;
import Engine.Render.entity;
import Engine.World;
import Engine.Portal;
import Engine.Portal.view;
import Engine.Physics;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.CameraRig;
import Engine.Animation;
import Engine.Jobs;
import Engine.Profiler;
import Engine.inputactions;
import Engine.glm;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.reflection;
import Engine.Render.custom;
import Engine.Replay;
import Engine.Perf;
import Engine.Params;
import Engine.Json;
import Engine.Streaming;
import Engine.Delta;
import Engine.PortalBridge;
import Engine.State;
import Engine.Storylet;
import Engine.Observation;
import Engine.Save;
import Engine.Render.observation;
import Engine.Terrain;

export enum class Phase { PreUpdate, FixedUpdate, Update, PostUpdate };

export using SystemFn = std::function<void(World&, float)>;

export struct WindowConfig {
    std::string title = "Lit Engine";
    int width = 1280;
    int height = 720;
};

export struct FlyCameraSettings {
    float speed = 4.0f;
    float sprintMultiplier = 5.0f;
    int captureMouseKey = 'T';
};

export struct FloatingOriginConfig {
    bool enabled = true;
    double threshold = 2000.0;
    double snap = 1024.0;
};

export class Engine {
  public:
    Engine();
    ~Engine();

    bool open(const WindowConfig& config = {});
    int run(const std::function<void(float)>& frame = {});
    bool openHeadless();
    [[nodiscard]] bool headless() const { return m_headless; }
    int runHeadless(uint64_t ticks = 0, float deltaTime = 0.0f, const std::function<void(uint64_t)>& beforeTick = {});
    void seed(uint64_t value) { m_world.random().seed(value); }
    void setInput(const InputState& state) { m_inputPending = state; }
    [[nodiscard]] const SimInput& simInput() const { return m_simInput; }
    bool startRecording(const std::string& path);
    bool stopRecording();
    [[nodiscard]] bool recording() const { return m_recorder.active(); }
    bool startReplay(const std::string& path);
    bool startReplay(ReplayData data);
    [[nodiscard]] bool replaying() const { return m_player.active(); }
    [[nodiscard]] uint64_t replaySeed() const { return m_player.seed(); }
    [[nodiscard]] size_t replayPosition() const { return m_player.position(); }
    void enablePerfLog(const std::string& path);
    void disablePerfLog();
    [[nodiscard]] PerfLog& perfLog() { return m_perf; }
    [[nodiscard]] ParamBus& params() { return m_params; }
    bool loadPostFx(const std::string& path, std::string* error = nullptr);
    void watchData(const std::string& path, std::function<bool(const std::string&)> reload);
    [[nodiscard]] uint32_t dataReloads() const { return m_dataReloads; }
    [[nodiscard]] std::vector<std::string> watchedFiles() const;
    [[nodiscard]] Json dumpState();
    [[nodiscard]] std::string dumpStateText(bool pretty = false);
    bool writeStateDump(const std::string& path);
    void enableFlyCamera(const FlyCameraSettings& settings = {});
    CharacterController& enableCharacter(const CharacterSettings& settings = {}, const CameraRigSettings& rig = {});
    [[nodiscard]] CharacterController* character() { return m_character.get(); }
    void setMouseCaptured(bool captured);
    bool enableImGui();
    void beginUi();
    [[nodiscard]] bool imguiEnabled() const { return m_renderer.imguiEnabled(); }
    [[nodiscard]] bool isMouseCaptured() const { return m_mouseCaptured; }
    [[nodiscard]] GLFWwindow* window() const { return m_window; }
    void init(GLFWwindow* window, const int windowWidth, const int windowHeight);
    void update(SceneDatabase& sceneDatabase, Camera& camera);
    void tick(float deltaTime);
    void reloadShaders();
    void setShaderWatch(bool enabled) { m_shaderWatch = enabled; }
    [[nodiscard]] const TimeState& time() const { return m_world.time(); }
    [[nodiscard]] PhysicsSettings& physics() { return m_physics; }
    [[nodiscard]] AnimationLibrary& animations() { return m_animations; }
    [[nodiscard]] JobSystem& jobs() { return m_jobs; }
    [[nodiscard]] Profiler& profiler() { return m_profiler; }
    void addSystem(Phase phase, std::string name, SystemFn system, bool runWhenPaused = false);
    bool removeSystem(const std::string& name);
    bool setSystemEnabled(const std::string& name, bool enabled);
    [[nodiscard]] bool isSystemEnabled(const std::string& name) const;
    [[nodiscard]] std::vector<std::string> systemNames(Phase phase) const;
    void setTimeScale(float scale) { m_world.timeState().timeScale = scale < 0.0f ? 0.0f : scale; }
    void setPaused(bool paused) { m_world.timeState().paused = paused; }
    void debugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color = glm::vec4(1.0f), bool overlay = false);
    void debugBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::vec4& color = glm::vec4(1.0f), bool overlay = false);
    void debugSphere(const glm::vec3& center, float radius, const glm::vec4& color = glm::vec4(1.0f));
    void debugRay(const Ray& ray, float length, const glm::vec4& color = glm::vec4(1.0f));
    void debugHierarchy(const glm::vec4& color = glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
    [[nodiscard]] std::optional<RayHit> pick(float screenX, float screenY);
    [[nodiscard]] Ray screenRay(float screenX, float screenY);
    [[nodiscard]] std::optional<glm::vec2> worldToScreen(const glm::vec3& point);
    [[nodiscard]] glm::vec2 windowSize() const { return glm::vec2(static_cast<float>(m_windowWidth), static_cast<float>(m_windowHeight)); }
    void setFixedTimestep(float seconds) {
        if (seconds > 0.0f) m_fixedStep = seconds;
    }
    void update();
    void update(World& world);
    [[nodiscard]] World& world() { return m_world; }
    [[nodiscard]] PortalSystem& portals() { return m_portals; }
    [[nodiscard]] const std::vector<VisibleCell>& visibleCells() const { return m_visibleCells; }
    StreamingSystem& enableStreaming(const StreamingConfig& config = {});
    [[nodiscard]] StreamingSystem* streaming() { return m_streaming.get(); }
    // Large world (docs/LargeWorld.md): local = world - worldOrigin(). Renderer consumes worldOrigin() / originEpoch().
    void enableFloatingOrigin(const FloatingOriginConfig& config = {});
    bool rebaseOrigin(const glm::dvec3& delta);
    [[nodiscard]] const glm::dvec3& worldOrigin() const { return m_world.origin(); }
    // Called with the float shift (positions become p - shift) after each rebase: hook NavMesh/NavCrowd/AudioPropagation/AudioMixer::shiftOrigin here.
    void addOriginShiftListener(std::function<void(const glm::vec3&)> listener) { m_originListeners.push_back(std::move(listener)); }
    [[nodiscard]] uint64_t originEpoch() const { return m_originEpoch; }
    [[nodiscard]] glm::dvec3 lastOriginShift() const { return m_lastOriginShift; }
    [[nodiscard]] glm::dvec3 cameraWorldPosition() { return m_world.toWorld(m_world.camera().getPosition()); }
    void setCameraWorldPosition(const glm::dvec3& position);
    // GPU mesh streaming: request(name, lod) loads on a worker, uploads under the streaming budget, evicts LRU over meshResidency().budget().
    MeshResidency& enableMeshStreaming(size_t budgetBytes = 256u << 20);
    [[nodiscard]] MeshResidency* meshResidency() { return m_meshResidency.get(); }
    bool requestMesh(const std::string& name, int lod = 0, int priority = 0, size_t estimatedBytes = 1u << 20);
    std::function<void(const std::string& name, uint32_t meshId, int lod)> onMeshResident;
    // Terrain (docs/Terrain.md): tiles stream around the camera, render with terrain.surface (morph + splat), merged scatter batches, shadows via the normal caster paths.
    TerrainStreamer& enableTerrain(const TerrainSetup& setup);
    [[nodiscard]] TerrainStreamer* terrain() { return m_terrain.get(); }
    // Raw mesh upload (no simplification, no point impostor) and release of the GPU geometry of any uploaded mesh (slots and buffer ranges are reused).
    uint32_t uploadTerrainMesh(const Mesh& mesh);
    void releaseMesh(uint32_t id);
    DeltaStore& enableDeltas();
    [[nodiscard]] DeltaStore* deltas() { return m_deltas.get(); }
    StateStore& enableState(const std::string& schemaPath = {});
    [[nodiscard]] StateStore* state() { return m_state.get(); }
    StoryletEngine& enableStorylets(const std::string& storyletPath = {});
    [[nodiscard]] StoryletEngine* storylets() { return m_storylets.get(); }
    ObservationSystem& enableObservation(const ObservationConfig& config = {}, bool gpuEnabled = true);
    [[nodiscard]] ObservationSystem* observation() { return m_observation.get(); }
    SaveSystem& enableSave(const std::string& autosaveDir = {}, float autosaveSeconds = 0.0f, uint32_t slots = 3);
    [[nodiscard]] SaveSystem* save() { return m_save.get(); }
    [[nodiscard]] const std::vector<PortalViewStats>& portalStats() const { return m_renderer.portalStats(); }
    [[nodiscard]] InputActions& input() { return m_input; }
    [[nodiscard]] Camera& camera() { return m_world.camera(); }
    void present();
    void cleanup();
    uint32_t uploadMesh(const Mesh& mesh);
    uint32_t uploadProcessedMesh(const Mesh& optimizedMesh, const std::vector<Mesh>& lodChain);
    // Uploads base mesh, LOD chain and (if present) the cluster LOD DAG used for dense geometry.
    uint32_t uploadProcessedMesh(const ProcessedMesh& processed);
    [[nodiscard]] ClusterRenderStats clusterStats() const { return m_renderer.clusterStats(); }
    uint32_t loadMesh(const std::string& name);
    void preloadMeshes(const std::vector<std::string>& names);
    void preloadSceneMeshes(const std::string& scenePath);
    [[nodiscard]] std::string meshName(uint32_t id) const;
    [[nodiscard]] std::vector<std::string> availableMeshes() const;
    [[nodiscard]] std::vector<GpuPassTiming> gpuTimings() const { return m_renderer.gpuTimings(); }
    [[nodiscard]] double gpuTotalMilliseconds() const { return m_renderer.gpuTotalMilliseconds(); }
    // Render graph (docs/RenderGraph.md): statistics of the last graph frame and a text + Graphviz dump of the next one.
    [[nodiscard]] RenderGraphInfo renderGraphInfo() const { return m_renderer.renderGraphInfo(); }
    void requestRenderGraphDump(std::string path) { m_renderer.requestGraphDump(std::move(path)); }
    [[nodiscard]] size_t renderOverCapacity() const { return m_renderer.overCapacity(); }
    void bakeReflectionProbes() { m_renderer.bakeProbes(); }
    void setProfilingHud(bool enabled) { m_renderer.setProfilingHud(enabled); }
    void requestScreenshot(std::string path) { m_renderer.requestScreenshot(std::move(path)); }
    uint32_t createMaterial(const glm::vec3& color, float strength = 1.0f);
    void updateMaterial(uint32_t id, const glm::vec3& color, float strength = 1.0f);
    uint32_t createMaterial(const Material& material);
    void setMaterial(uint32_t id, const Material& material);
    [[nodiscard]] const Material& material(uint32_t id) const;
    [[nodiscard]] size_t materialCount() const { return m_materials.size(); }
    [[nodiscard]] std::vector<std::string> shaderNames() const { return m_renderer.surfaceNames(); }
    uint32_t loadShader(const std::string& name, const std::string& path = {});
    [[nodiscard]] std::string shaderStatus(const std::string& name) const { return m_renderer.surfaceStatus(name); }
    [[nodiscard]] std::vector<ShaderError> shaderErrors() const { return m_renderer.surfaceErrors(); }
    [[nodiscard]] std::vector<ShaderError> takeShaderErrors() { return ::takeShaderErrors(); }
    int loadTexture(const std::string& path, bool srgb = true);
    [[nodiscard]] RenderSettings& renderSettings() { return m_renderer.settings(); }
    uint32_t addCompute(const ComputeDesc& desc) { return m_renderer.custom().addCompute(desc); }
    uint32_t addDraw(const DrawDesc& desc) { return m_renderer.custom().addDraw(desc); }
    uint32_t addEffect(const std::string& name, const std::string& path = {}) { return m_renderer.custom().addEffect({name, path.empty() ? "resources/shaders/effects/" + name + ".effect" : path}); }
    template <typename T>
    void setShaderBuffer(const std::string& name, const std::vector<T>& data) { m_renderer.custom().setBuffer(name, sizeof(T), static_cast<uint32_t>(data.size()), data.data()); }
    void setShaderBuffer(const std::string& name, uint32_t stride, uint32_t count, const void* data = nullptr) { m_renderer.custom().setBuffer(name, stride, count, data); }
    [[nodiscard]] std::vector<uint8_t> readShaderBuffer(const std::string& name) { return m_renderer.custom().readBuffer(name); }
    bool createShaderTexture(const std::string& name, int width, int height, ShaderTextureFormat format = ShaderTextureFormat::RGBA16F) { return m_renderer.custom().createTexture(name, width, height, format); }
    bool loadShaderTexture(const std::string& name, const std::string& path, bool srgb = true) { return m_renderer.custom().loadTexture(name, path, srgb); }
    void setPassParam(PassKind kind, uint32_t id, int index, const glm::vec4& value) { m_renderer.custom().setParam(kind, id, index, value); }
    void setPassEnabled(PassKind kind, uint32_t id, bool enabled) { m_renderer.custom().setEnabled(kind, id, enabled); }
    [[nodiscard]] std::vector<PassInfo> customPasses() const { return const_cast<Renderer&>(m_renderer).custom().passes(); }
    [[nodiscard]] bool meshShadersSupported() const { return const_cast<Renderer&>(m_renderer).custom().meshShadersSupported(); }
    [[nodiscard]] float exposureEv100() const { return m_renderer.currentEv100(); }
    [[nodiscard]] glm::vec3 sunDirection() const { return m_renderer.sunDirection(); }
    void uploadBasePositions(const std::vector<glm::vec3>& basePositions);
    void setAnimation(float time, uint32_t movingCount, uint32_t entityOffset);
    // Dynamic (GPU skinned, morphed or deformed) meshes, see docs/Skinning.md. All are no-ops without a window.
    // createDynamicSource uploads rest data once; createDynamicInstance returns an instance id whose mesh id
    // (dynamicMeshId) goes into a RenderableComponent. Tie it to the entity so culling uses the refit bounds.
    uint32_t createDynamicSource(const DynamicMesh& mesh) { return m_renderer.createDynamicSource(mesh); }
    uint32_t createDynamicInstance(uint32_t source) { return m_renderer.createDynamicInstance(source); }
    [[nodiscard]] uint32_t dynamicMeshId(uint32_t instance) const { return m_renderer.dynamicMeshId(instance); }
    void setDynamicEntity(uint32_t instance, EntityHandle entity) { m_renderer.setDynamicEntity(instance, entity.index); }
    void setDynamicPose(uint32_t instance, std::span<const glm::mat4> palette) { m_renderer.setDynamicPose(instance, palette); }
    void setDynamicMorph(uint32_t instance, std::span<const float> weights) { m_renderer.setDynamicMorph(instance, weights); }
    void setDynamicDeform(uint32_t instance, const DeformSettings& settings) { m_renderer.setDynamicDeform(instance, settings); }
    [[nodiscard]] glm::vec4 dynamicBounds(uint32_t instance) const { return m_renderer.dynamicBounds(instance); }
    [[nodiscard]] DynamicMeshStats dynamicStats() const { return m_renderer.dynamicStats(); }
    void AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color);
    void setLodBias(float bias);
    void setForcedLod(int lod);
    int getForcedLod() const;
    void setSmallObjectThreshold(float threshold);
    void setDebugDepthMode(bool enabled);
    bool isDebugDepthMode() const;
    void setFullProfiling(bool enabled);
    [[nodiscard]] std::vector<GpuEntityState> readGpuEntityStates() { return m_renderer.readGpuEntityStates(m_world.database().transforms.size()); }

  private:
    struct System {
        Phase phase;
        std::string name;
        SystemFn fn;
        bool enabled = true;
        bool runWhenPaused = false;
    };

    void runSystems(Phase phase, float deltaTime, bool paused);
    void beginTickInput(float& deltaTime);
    void uploadParams();
    void applyPostFx();
    void pollData();
    void applyWorldAnimation();
    void updatePortals(World& world);
    void syncWindowSize(Camera& camera);

    GLFWwindow* m_window = nullptr;
    bool m_ownsWindow = false;
    bool m_mouseCaptured = false;
    bool m_headless = false;
    SimInput m_simInput;
    InputState m_inputPending;
    InputRecorder m_recorder;
    InputPlayer m_player;
    std::string m_recordPath;
    PerfLog m_perf;
    ParamBus m_params;
    uint64_t m_paramsUploaded = ~0ull;
    struct PostFxBinding {
        std::string key;
        float base[3] = {0.0f, 0.0f, 0.0f};
        int count = 1;
        std::string param;
    };
    struct DataWatch {
        std::string path;
        long long stamp = 0;
        std::function<bool(const std::string&)> reload;
    };
    std::vector<PostFxBinding> m_postFx;
    std::vector<DataWatch> m_dataWatches;
    uint32_t m_dataReloads = 0;
    uint32_t m_paramPollCounter = 0;
    std::chrono::steady_clock::time_point m_lastTickStart{};
    double m_lastSimMs = 0.0;
    Renderer m_renderer;
    JobSystem m_jobs;
    Profiler m_profiler;
    World m_world;
    PortalSystem m_portals;
    std::vector<VisibleCell> m_visibleCells;
    std::unique_ptr<StreamingSystem> m_streaming;
    std::unique_ptr<MeshResidency> m_meshResidency;
    std::unique_ptr<TerrainStreamer> m_terrain;
    std::map<std::string, std::map<int, uint32_t>> m_residentMeshIds;
    FloatingOriginConfig m_originConfig;
    uint64_t m_originEpoch = 0;
    std::vector<std::function<void(const glm::vec3&)>> m_originListeners;
    glm::dvec3 m_lastOriginShift{0.0};
    std::unique_ptr<DeltaStore> m_deltas;
    std::unique_ptr<StateStore> m_state;
    std::unique_ptr<StoryletEngine> m_storylets;
    std::unique_ptr<ObservationSystem> m_observation;
    std::unique_ptr<SaveSystem> m_save;
    bool m_gpuObservation = false;
    void feedGpuObservation();
    void submitGpuObservation();
    int m_windowWidth = 1280;
    int m_windowHeight = 720;
    InputActions m_input;
    std::vector<glm::vec3> m_animationBase;
    uint32_t m_animationOffset = 0;
    uint32_t m_animationCount = 0;
    std::vector<Material> m_materials = std::vector<Material>(1);
    std::vector<std::string> m_texturePaths[2];
    std::unordered_map<int, int> m_textureRemap[2];
    std::unordered_map<uint32_t, uint32_t> m_surfaceRemap;
    void registerSceneSections();
    void uploadMaterial(uint32_t id);
    struct MeshLocalBox {
        glm::vec3 center{0.0f};
        glm::vec3 halfExtents{0.0f};
    };
    std::unordered_map<uint32_t, MeshLocalBox> m_meshBoxes;
    void recordMeshBox(uint32_t id, const Mesh& mesh);
    uint32_t registerProcessedMesh(const std::string& name, const ProcessedMesh& processed);
    ReflectionScene m_reflectionScene;
    uint64_t m_reflectionFrame = 0;
    void refreshReflectionScene();
    bool m_shaderWatch = false;
    std::chrono::steady_clock::time_point m_lastShaderPoll{};
    std::unordered_map<std::string, long long> m_shaderStamps;
    bool shadersChanged();
    PhysicsSettings m_physics;
    std::unique_ptr<CharacterController> m_character;
    AnimationLibrary m_animations;
    std::vector<System> m_systems;
    float m_fixedStep = 1.0f / 60.0f;
    float m_accumulator = 0.0f;
    std::unordered_map<std::string, uint32_t> m_meshIds;
    std::unordered_map<uint32_t, std::string> m_meshNames;
};