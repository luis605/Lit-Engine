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

// ---------------------------------------------------------------- height sources

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

// ---------------------------------------------------------------- config and layers

export enum class TerrainLayer : uint8_t { Grass = 0, Rock = 1, Sand = 2, Snow = 3 };

export struct SplatRules {
    float sandMaxHeight = 3.0f;
    float sandBlend = 2.0f;
    float snowMinHeight = 95.0f;
    float snowBlend = 12.0f;
    float rockMinSlope = 0.62f;
    float rockFullSlope = 0.82f;
};

// Weights as (grass, rock, sand, snow), summing to one.
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

// ---------------------------------------------------------------- tiles and LOD

// A tile covers [x, x + 1) * size by [z, z + 1) * size where size = tileSize << level.
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

export struct LodParams {
    int maxLevel = 5;
    float splitFactor = 2.0f;
    float morphStart = 0.7f;
    float viewDistance = 4096.0f;
    // 2:1 balance: neighbouring leaves differ by at most one level (the coarser leaf is split until they do).
    bool balance = true;
    // Frustum culling (world space planes, normals pointing inside, see frustumPlanes). Tiles within keepRadius of the
    // camera or within frustumMargin of the frustum stay selected so shadow casters behind the view and quick turns do not pop.
    bool frustumCull = false;
    std::array<glm::vec4, 6> planes{};
    float frustumMargin = 192.0f;
    float keepRadius = 192.0f;
};

// Planes (xyz normal pointing inside, w offset) of a view-projection matrix (Vulkan style 0..1 depth), normalized.
export [[nodiscard]] std::array<glm::vec4, 6> frustumPlanes(const glm::mat4& viewProjection);
// Positive when the box is at least partly inside every plane expanded by margin.
export [[nodiscard]] bool boxInFrustum(const std::array<glm::vec4, 6>& planes, const glm::vec3& low, const glm::vec3& high, float margin = 0.0f);

export struct SelectedTile {
    TileKey key;
    float distance = 0.0f;
    float morph = 0.0f;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

// Pure function. Deterministic order: roots by (z, x), children in z-major order.
export [[nodiscard]] std::vector<SelectedTile> selectTerrainTiles(const TerrainConfig& config, const glm::vec3& camera, const LodParams& lod = {});

export struct TileMeshOptions {
    int stride = 1;
    bool skirts = true;
    bool flipWinding = false;
    // Packs the per-vertex morph delta into the normal length (|n| = 1 + delta / kTerrainMorphScale) for
    // resources/shaders/surfaces/terrain.surface. Other shaders only see a slightly longer normal.
    bool packMorph = false;
};

export inline constexpr float kTerrainMorphScale = 256.0f;

// Vertex layout is the engine's (position, normal, uv): positions are local to tileOrigin. The splat, morph and
// bounds are side data that a renderer may upload as a second stream or bake to a texture.
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
// RGBA8 splat weights per texel for texture-based material blending (virtual texturing input).
export [[nodiscard]] std::vector<uint8_t> bakeTileSplat(const TerrainConfig& config, const TileKey& key, int resolution);

// ---------------------------------------------------------------- scatter

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
    // Optional CPU geometry (unit scale, origin at the base). With it, instances of this species are merged into one
    // mesh per tile (scatterBatches) instead of one entity each.
    std::shared_ptr<const Mesh> geometry;
    uint32_t shader = 0;
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

// Candidates live on a global jittered grid per species and every candidate draws a fixed number of numbers from a
// Pcg32 seeded by (seed, species, cell), so the result does not depend on tile borders, LOD or call order.
export [[nodiscard]] std::vector<ScatterInstance> scatterTile(const TerrainConfig& config, const ScatterConfig& scatter, const TileKey& key);
export [[nodiscard]] float terrainBiome(const ScatterConfig& scatter, float x, float z);
// Stable key of a scatter instance: seed, species index and global grid cell (independent of tile borders and LOD).
export [[nodiscard]] std::string scatterInstanceKey(const ScatterConfig& scatter, uint32_t species, int32_t cellX, int32_t cellZ);
export [[nodiscard]] std::string scatterInstanceKey(const ScatterConfig& scatter, const ScatterInstance& instance);
export [[nodiscard]] std::vector<CellEntity> scatterEntities(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances);

// A tile's instances of one species merged into a single static mesh (vertices local to the tile origin). The mesh goes
// through the ordinary mesh path (LOD chain, point impostor, cluster LOD DAG when large), so a whole tile of trees or
// rocks is one entity: culled, LOD-switched, shadowed and Hi-Z tested by the existing instanced pipeline.
export struct ScatterBatch {
    uint32_t species = 0;
    uint32_t instances = 0;
    Mesh mesh{std::vector<float>{}, std::vector<unsigned int>{}};
    std::vector<glm::vec3> positions;
};
export [[nodiscard]] std::vector<ScatterBatch> scatterBatches(const ScatterConfig& scatter, const TileKey& key, const std::vector<ScatterInstance>& instances, const TerrainConfig& terrain);

// ---------------------------------------------------------------- streaming

// Mesh prepared on a worker (optimized, LOD chain, clusters) for ScatterBatch uploads.
export struct PreparedMesh {
    Mesh base{std::vector<float>{}, std::vector<unsigned int>{}};
    std::vector<Mesh> lods;
    ClusterMesh clusters;
};

export struct TerrainLoaderConfig {
    TerrainConfig terrain;
    ScatterConfig scatter;
    std::function<std::optional<TileKey>(int cell)> tileOfCell;
    // Tile mesh upload (Engine::uploadTerrainMesh: raw, no simplification).
    std::function<uint32_t(const Mesh&)> uploadMesh;
    // Merged scatter mesh upload (Engine::uploadPrepared: LOD chain, impostor, clusters). Null: one entity per instance.
    std::function<uint32_t(PreparedMesh&)> uploadScatterMesh;
    // Frees the GPU geometry of an uploaded mesh when its cell unloads (Engine::releaseMesh).
    std::function<void(uint32_t)> releaseMesh;
    // Material per LOD level (terrain.surface needs per-level morph parameters); falls back to `material`.
    std::function<uint32_t(int level)> materialOfLevel;
    uint32_t material = 0;
    uint32_t shader = 0;
    uint32_t layer = 1;
    int colliderMaxLevel = 0;
    int scatterMaxLevel = 0;
    TileMeshOptions meshOptions;
    // Entities are created hidden; the streamer reveals them (no overlap of old and new LOD).
    bool startHidden = false;
    bool scatterClusters = false;
    // Player changes to scatter (removed or moved instances, objects added with TerrainTileLoader::trackAdded) are kept here per tile
    // (cell name terrainCellName) and survive unload/reload and save/load. Only tiles with level <= scatterMaxLevel take part.
    DeltaStore* deltas = nullptr;
};

// Delta cell of a tile: "terrain/x_z_level".
export [[nodiscard]] std::string terrainCellName(const TileKey& key);

// Resumable tile loader used by makeTerrainLoader and TerrainStreamer.
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
    [[nodiscard]] TerrainLoaderConfig& config();

    // Removes one scatter instance of a resident tile (cell id of the loader) and records it in the DeltaStore. A per-instance entity is destroyed;
    // an instance merged into a batch is dropped by rebuilding that batch mesh (new mesh upload, old one released). False when the tile is not resident.
    bool removeScatter(int cell, uint32_t species, int32_t cellX, int32_t cellZ);
    // Records an entity (subtree root) the player added to a resident tile so it is saved and recreated when the tile reloads.
    bool trackAdded(int cell, EntityHandle root);
    // Rebuilds batches of resident tiles whose removed set changed in the DeltaStore (after DeltaStore::fromJson). Cheap when nothing changed.
    void syncDeltas();
    [[nodiscard]] std::string cellName(int cell) const;
    [[nodiscard]] std::optional<int> residentCellOf(const TileKey& key) const;

  private:
    struct State;
    void rebuild(int cell);
    void finish(int cell, void* payload);
    std::shared_ptr<State> m_state;
};

// CellLoader contract of docs/Streaming.md: prepare (worker: heights, mesh, collider, scatter), commit (main thread:
// mesh upload, entity creation, resumable under the budget), unload (batched destroy, GPU meshes released).
export [[nodiscard]] CellLoader makeTerrainLoader(World& world, TerrainLoaderConfig config);

// ---------------------------------------------------------------- tile streamer

export struct TerrainStreamConfig {
    LodParams lod;
    uint32_t workers = 2;
    uint32_t maxInFlight = 6;
    double commitMillisPerFrame = 2.0;
    size_t uploadBytesPerFrame = 8u << 20;
    // An unwanted tile is kept this many updates (it may be needed again after a turn); it is also kept while it still covers a wanted tile that is not resident yet.
    uint32_t evictDelay = 90;
    // Re-select only when the camera moved or turned this much (metres / dot of view direction) or after reselectInterval updates.
    float moveThreshold = 1.0f;
    uint32_t reselectInterval = 30;
    uint32_t maxResidentTiles = 1024;
    // The renderer has 4096 mesh slots, 7 per mesh: unwanted tiles are evicted early above this many GPU meshes and commits pause 40 above it.
    uint32_t maxGpuMeshes = 420;
    // Wait for every worker job each update and commit/unload without a time budget: residency then depends only on the
    // camera path (fixed-step headless runs and replays hash identically). Costs frame time; meant for headless and tests.
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
    uint32_t gpuMeshes = 0;
    uint32_t entities = 0;
    bool sharedBudget = false;
    double updateMillis = 0.0;
};

// Drives tile residency from the camera: selectTerrainTiles (frustum culling, 2:1 balance) -> worker prepare ->
// budgeted commit -> visibility swap without holes or overlap -> delayed eviction that releases GPU meshes.
export class TerrainStreamer {
  public:
    TerrainStreamer(World& world, TerrainLoaderConfig loader, TerrainStreamConfig config = {});
    ~TerrainStreamer();
    TerrainStreamer(const TerrainStreamer&) = delete;
    TerrainStreamer& operator=(const TerrainStreamer&) = delete;

    // cameraWorld: absolute camera position. planes: world-space frustum planes (frustumPlanes of the world-space view-projection) or null.
    void update(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes);
    // Synchronous load of everything wanted at this position (start-up, teleport, tests).
    void prime(const glm::vec3& cameraWorld, const glm::vec3& cameraForward, const std::array<glm::vec4, 6>* planes);
    [[nodiscard]] const TerrainStats& stats() const;
    [[nodiscard]] std::vector<TileKey> residentTiles() const;
    [[nodiscard]] std::vector<TileKey> visibleTiles() const;
    [[nodiscard]] std::vector<SelectedTile> wantedTiles() const;
    [[nodiscard]] bool settled() const;
    [[nodiscard]] TerrainConfig& terrain();
    [[nodiscard]] TerrainStreamConfig& config();
    [[nodiscard]] TerrainTileLoader& loader();
    // Shared per-frame budget (Engine wires StreamingSystem::sharedBudget): commits and unloads draw from it instead of their own, so portal
    // cells, assets and terrain tiles never spend the frame twice. Null result (or no provider): the own commitMillisPerFrame/uploadBytesPerFrame.
    void setSharedBudget(std::function<StreamBudget*()> provider);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// ---------------------------------------------------------------- engine integration

// Engine::enableTerrain: terrain.surface shader, one material per LOD level, tile streamer, GPU mesh release.
export struct TerrainSetup {
    TerrainConfig terrain;
    ScatterConfig scatter;
    TerrainStreamConfig stream;
    uint32_t layer = 1;
    int colliderMaxLevel = 0;
    int scatterMaxLevel = 0;
    // Build the cluster LOD DAG for large merged scatter meshes. Off by default: the renderer cluster store is append-only, so streaming leaks it.
    bool scatterClusters = false;
    TileMeshOptions mesh;  // packMorph is forced on
    std::string shaderPath = "resources/shaders/surfaces/terrain.surface";
    // Optional layer textures (grass, rock, sand, snow); empty entries use the procedural look.
    std::array<std::string, 4> albedoTextures;
    std::array<std::string, 4> normalTextures;
    // Load the visible area synchronously before returning (start-up, never a loading screen at runtime).
    bool prime = true;
};
