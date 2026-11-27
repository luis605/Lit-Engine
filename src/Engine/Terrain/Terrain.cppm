module;

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

export module Engine.Terrain;

import Engine.glm;
import Engine.Delta;
import Engine.mesh;
import Engine.meshlet;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Streaming;
import Engine.World;
export import Engine.Instancing;

export struct NoiseParams {
    uint64_t seed = 1;
    float frequency = 1.0f / 600.0f;
    int octaves = 8;
    float lacunarity = 2.02f;
    float gain = 0.5f;
    float amplitude = 140.0f;
    float baseHeight = 0.0f;
    float warp = 0.35f;
    float ridgeMix = 0.55f;
    float erosion = 1.6f;
    float flatten = 1.35f;
};

export [[nodiscard]] float terrainValueNoise(uint64_t seed, float x, float z);

export using HeightFn = std::function<float(float, float)>;

export [[nodiscard]] HeightFn makeNoiseHeight(const NoiseParams& params);

export class HeightImage {
  public:
    int width = 0;
    int depth = 0;
    std::vector<uint16_t> samples;

    [[nodiscard]] bool valid() const { return width >= 2 && depth >= 2 && samples.size() == static_cast<size_t>(width) * static_cast<size_t>(depth); }
    [[nodiscard]] float sample01(float u, float v) const;

    [[nodiscard]] static std::optional<HeightImage> fromRaw16(const std::vector<uint8_t>& bytes, int width, int depth, bool bigEndian = false);
    [[nodiscard]] static std::optional<HeightImage> fromPgm(const std::vector<uint8_t>& bytes);
    [[nodiscard]] static std::optional<HeightImage> loadRaw16(const std::string& path, int width, int depth, bool bigEndian = false);
    [[nodiscard]] static std::optional<HeightImage> loadPgm(const std::string& path);
};

export struct HeightImagePlacement {
    glm::vec2 origin{0.0f};
    float cellSize = 1.0f;
    float minHeight = 0.0f;
    float maxHeight = 100.0f;
};

export [[nodiscard]] HeightFn makeImageHeight(std::shared_ptr<const HeightImage> image, const HeightImagePlacement& placement);

export enum class TerrainLayer : uint8_t { Grass = 0, Rock = 1, Sand = 2, Snow = 3 };

export struct SplatRules {
    float sandMaxHeight = 3.0f;
    float sandBlend = 2.0f;
    float snowMinHeight = 95.0f;
    float snowBlend = 12.0f;
    float rockMinSlope = 0.62f;
    float rockFullSlope = 0.82f;
};

export [[nodiscard]] glm::vec4 splatWeights(float height, float slope, const SplatRules& rules);
export [[nodiscard]] TerrainLayer dominantLayer(const glm::vec4& weights);

export struct TerrainConfig {
    HeightFn height;
    float tileSize = 64.0f;
    int tileCells = 64;
    float minHeight = -20.0f;
    float maxHeight = 200.0f;
    float skirtDepth = 1.5f;
    float uvScale = 1.0f / 8.0f;
    SplatRules splat;
};

export [[nodiscard]] float terrainHeight(const TerrainConfig& config, float x, float z);
export [[nodiscard]] glm::vec3 terrainNormal(const TerrainConfig& config, float x, float z, float step = 1.0f);

export struct TileKey {
    int x = 0;
    int z = 0;
    int level = 0;
    bool operator==(const TileKey&) const = default;
    auto operator<=>(const TileKey&) const = default;
};

export [[nodiscard]] float tileWorldSize(const TerrainConfig& config, const TileKey& key);
export [[nodiscard]] glm::vec3 tileOrigin(const TerrainConfig& config, const TileKey& key);
export [[nodiscard]] std::string tileName(const TileKey& key);

export struct TerrainViewpoint {
    glm::vec3 position{0.0f};
    std::array<glm::vec4, 6> planes{};
};

export struct LodParams {
    int maxLevel = 5;
    float splitFactor = 2.0f;
    float morphStart = 0.7f;
    float viewDistance = 4096.0f;
    bool balance = true;
    bool frustumCull = false;
    std::array<glm::vec4, 6> planes{};
    float frustumMargin = 192.0f;
    float keepRadius = 192.0f;
    std::vector<TerrainViewpoint> extraViews;
};

export [[nodiscard]] std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& viewProjection);
export [[nodiscard]] bool boxInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& low, const glm::vec3& high, float margin = 0.0f);

export struct SelectedTile {
    TileKey key;
    float distance = 0.0f;
    float morph = 0.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

export [[nodiscard]] std::vector<SelectedTile> selectTerrainTiles(const TerrainConfig& config, const glm::vec3& camera, const LodParams& lod = {});

export struct TileMeshOptions {
    int stride = 1;
    bool skirts = true;
    bool flipWinding = false;
    bool packMorph = false;
};

export inline constexpr float kTerrainMorphScale = 256.0f;

export struct TerrainTileMesh {
    Mesh mesh;
    std::vector<glm::vec4> splat;
    std::vector<float> morph;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    uint32_t surfaceVertices = 0;
    TerrainLayer dominant = TerrainLayer::Grass;
};

export [[nodiscard]] std::shared_ptr<HeightfieldData> buildTileHeightfield(const TerrainConfig& config, const TileKey& key);
export [[nodiscard]] TerrainTileMesh buildTileMesh(const TerrainConfig& config, const TileKey& key, const TileMeshOptions& options = {});
export [[nodiscard]] std::vector<uint8_t> bakeTileSplat(const TerrainConfig& config, const TileKey& key, int resolution);

export enum class ScatterKind : uint8_t { Tree, Rock, Grass };

export struct ScatterSpecies {
    std::string name;
    ScatterKind kind = ScatterKind::Tree;
    uint32_t mesh = 0;
    uint32_t material = 0;
    std::string tag;
    float spacing = 8.0f;
    float density = 0.6f;
    float minHeight = -1.0e9f;
    float maxHeight = 1.0e9f;
    float minNormalY = 0.8f;
    float minBiome = 0.0f;
    float maxBiome = 1.0f;
    float clump = 0.0f;
    float minScale = 0.8f;
    float maxScale = 1.3f;
    float yOffset = 0.0f;
    bool alignToSlope = false;
    std::shared_ptr<const Mesh> geometry;
    uint32_t shader = 0;
    float drawDistance = 0.0f;
    float fadeStart = 0.0f;
    bool castShadows = true;
};

export struct ScatterConfig {
    uint64_t seed = 1;
    float biomeFrequency = 1.0f / 300.0f;
    std::vector<ScatterSpecies> species;
};

export struct ScatterInstance {
    uint32_t species = 0;
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float yaw = 0.0f;
    float scale = 1.0f;
    float biome = 0.0f;
    uint32_t cellX = 0;
    uint32_t cellZ = 0;
};

export [[nodiscard]] std::vector<ScatterInstance> scatterTile(const TerrainConfig& config, const ScatterConfig& scatter, const TileKey& key);
export [[nodiscard]] float terrainBiome(const ScatterConfig& scatter, float x, float z);
export [[nodiscard]] std::string scatterInstanceKey(const ScatterConfig& scatter, uint32_t species, int32_t cellX, int32_t cellZ);
export [[nodiscard]] std::string scatterInstanceKey(const ScatterConfig& scatter, const ScatterInstance& instance);
export [[nodiscard]] std::vector<CellEntity> scatterEntities(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances);

export struct ScatterBatch {
    uint32_t species = 0;
    uint32_t instances = 0;
    Mesh mesh{std::vector<float>{}, std::vector<unsigned int>{}};
    std::vector<glm::vec3> positions;
};
export [[nodiscard]] std::vector<ScatterBatch> scatterBatches(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances, const TerrainConfig& terrain);

export struct PreparedMesh {
    Mesh base{std::vector<float>{}, std::vector<unsigned int>{}};
    std::vector<Mesh> lods;
    ClusterMesh clusters;
};

export struct TerrainLoaderConfig {
    TerrainConfig terrain;
    ScatterConfig scatter;
    std::function<std::optional<TileKey>(int cell)> tileOfCell;
    std::function<uint32_t(const Mesh&)> uploadMesh;
    std::function<uint32_t(PreparedMesh&)> uploadScatterMesh;
    std::function<void(uint32_t)> releaseMesh;
    std::function<uint32_t(int level)> materialOfLevel;
    uint32_t material = 0;
    uint32_t shader = 0;
    uint32_t layer = 1;
    int colliderMaxLevel = 0;
    int scatterMaxLevel = 0;
    TileMeshOptions meshOptions;
    bool startHidden = false;
    bool scatterClusters = false;
    DeltaStore* deltas = nullptr;
    bool instancedScatter = false;
    std::function<uint32_t(uint32_t species)> speciesMesh;
};

export [[nodiscard]] uint64_t scatterInstanceId(uint32_t species, int32_t cellX, int32_t cellZ);
export [[nodiscard]] InstanceData scatterInstanceData(const ScatterSpecies& species, const ScatterInstance& instance, const glm::vec3& origin);

export [[nodiscard]] std::string terrainCellName(const TileKey& key);

export class TerrainTileLoader {
  public:
    TerrainTileLoader(World& world, TerrainLoaderConfig config);
    ~TerrainTileLoader();
    TerrainTileLoader(const TerrainTileLoader&) = delete;
    TerrainTileLoader& operator=(const TerrainTileLoader&) = delete;

    [[nodiscard]] std::shared_ptr<void> prepare(int cell);
    bool commit(int cell, void* payload, StreamBudget& budget);
    bool unload(int cell, StreamBudget& budget);
    void setVisible(int cell, bool visible);
    [[nodiscard]] size_t entityCount(int cell) const;
    [[nodiscard]] size_t meshCount() const;
    [[nodiscard]] size_t scatterBatchBytes(int cell) const;
    [[nodiscard]] TerrainLoaderConfig& config();

    bool removeScatter(int cell, uint32_t species, int32_t cellX, int32_t cellZ);
    bool moveScatter(int cell, uint32_t species, int32_t cellX, int32_t cellZ, const glm::vec3& worldPosition, const glm::quat& rotation, float scale);
    [[nodiscard]] EntityHandle scatterBatchEntity(int cell, uint32_t species) const;
    bool trackAdded(int cell, EntityHandle root);
    void syncDeltas();
    [[nodiscard]] std::string cellName(int cell) const;
    [[nodiscard]] std::optional<int> residentCellOf(const TileKey& key) const;

  private:
    struct State;
    void rebuild(int cell);
    void finish(int cell, void* payload);
    std::shared_ptr<State> m_state;
};

export [[nodiscard]] CellLoader makeTerrainLoader(World& world, TerrainLoaderConfig config);

export struct TerrainStreamConfig {
    LodParams lod;
    uint32_t workers = 2;
    uint32_t maxInFlight = 6;
    double commitMillisPerFrame = 2.0;
    size_t uploadBytesPerFrame = 8u << 20;
    uint32_t evictDelay = 90;
    float moveThreshold = 1.0f;
    uint32_t reselectInterval = 30;
    uint32_t maxResidentTiles = 1024;
    uint32_t maxGpuMeshes = 420;
    bool deterministic = false;
};

export struct TerrainStats {
    uint32_t wanted = 0;
    uint32_t culledByFrustum = 0;
    uint32_t balanceSplits = 0;
    uint32_t resident = 0;
    uint32_t visible = 0;
    uint32_t loading = 0;
    uint32_t loaded = 0;
    uint32_t evicted = 0;
    uint32_t cancelled = 0;
    uint32_t memoryEvicted = 0;
    uint64_t residentBytes = 0;
    bool sharedPool = false;
    uint32_t gpuMeshes = 0;
    uint32_t entities = 0;
    bool sharedBudget = false;
    double updateMillis = 0.0;
};

export class TerrainStreamer {
  public:
    TerrainStreamer(World& world, TerrainLoaderConfig loader, TerrainStreamConfig config = {});
    ~TerrainStreamer();
    TerrainStreamer(const TerrainStreamer&) = delete;
    TerrainStreamer& operator=(const TerrainStreamer&) = delete;

    void update(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes, std::vector<TerrainViewpoint> extraViews = {});
    void prime(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes, std::vector<TerrainViewpoint> extraViews = {});
    [[nodiscard]] const TerrainStats& stats() const;
    [[nodiscard]] std::vector<TileKey> residentTiles() const;
    [[nodiscard]] std::vector<TileKey> visibleTiles() const;
    [[nodiscard]] std::vector<SelectedTile> wantedTiles() const;
    [[nodiscard]] bool settled() const;
    [[nodiscard]] TerrainConfig& terrain();
    [[nodiscard]] TerrainStreamConfig& config();
    [[nodiscard]] TerrainTileLoader& loader();
    void setSharedBudget(std::function<StreamBudget*()> provider);
    void attachResidency(ResidencyManager& manager);
    void detachResidency();
    [[nodiscard]] ResidencyManager& residency();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

export struct TerrainSetup {
    TerrainConfig terrain;
    ScatterConfig scatter;
    TerrainStreamConfig stream;
    uint32_t layer = 1;
    int colliderMaxLevel = 0;
    int scatterMaxLevel = 0;
    bool scatterClusters = false;
    bool instancedScatter = true;
    TileMeshOptions mesh;
    std::string shaderPath = "resources/shaders/surfaces/terrain.surface";
    std::array<std::string, 4> albedoTextures;
    std::array<std::string, 4> normalTextures;
    bool prime = true;
};
