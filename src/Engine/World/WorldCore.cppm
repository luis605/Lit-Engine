module;

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <istream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <typeinfo>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.World:Core;

import Engine.Render.entity;
import Engine.Events;
import Engine.Jobs;
import Engine.Profiler;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;
import Engine.Rng;
import :Types;
import :Pool;

export class EntityBuilder;

export class World {
  public:
    World();

    EntityHandle create(const EntityDesc& desc = {});
    EntityHandle create(std::string name, uint32_t mesh = NO_MESH, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), EntityHandle parent = NULL_ENTITY);
    template <typename Fn>
    void createBatch(size_t count, Fn&& fn) {
        reserve(m_alive.size() + count);
        EntityDesc desc;
        for (size_t index = 0; index < count; ++index) {
            desc = EntityDesc{};
            fn(index, desc);
            createImpl(desc);
        }
        touchStructure();
    }
    void destroy(EntityHandle entity);
    void destroyBatch(std::span<const EntityHandle> entities);
    void setPositions(std::span<const std::pair<EntityHandle, glm::vec3>> updates);
    std::vector<EntityMoved> compact();
    [[nodiscard]] bool isAlive(EntityHandle entity) const;
    [[nodiscard]] size_t aliveCount() const { return m_aliveCount; }
    void reserve(size_t count);
    [[nodiscard]] EntityHandle handleOf(Entity index) const;
    [[nodiscard]] static Entity slot(EntityHandle h) { return h.index; }

    void setParent(EntityHandle entity, EntityHandle parent, bool keepWorldTransform = true);
    [[nodiscard]] EntityHandle getParent(EntityHandle entity) const;
    [[nodiscard]] std::vector<EntityHandle> getChildren(EntityHandle entity) const;
    [[nodiscard]] std::vector<EntityHandle> getRoots() const;
    [[nodiscard]] EntityHandle firstRoot() const { return handleOf(m_firstRoot); }
    [[nodiscard]] EntityHandle firstChild(EntityHandle entity) const { return valid(entity) ? handleOf(m_db.firstChild[entity.index]) : NULL_ENTITY; }
    [[nodiscard]] EntityHandle nextSibling(EntityHandle entity) const { return valid(entity) ? handleOf(m_db.nextSibling[entity.index]) : NULL_ENTITY; }
    [[nodiscard]] bool isDescendantOf(EntityHandle entity, EntityHandle ancestor) const;

    void setName(EntityHandle entity, std::string name);
    [[nodiscard]] const std::string& getName(EntityHandle entity) const;
    [[nodiscard]] EntityHandle find(std::string_view name) const;
    [[nodiscard]] std::vector<EntityHandle> findAll(std::string_view name) const;

    void setLocalMatrix(EntityHandle entity, const glm::mat4& local);
    void setPosition(EntityHandle entity, const glm::vec3& position);
    void setRotation(EntityHandle entity, const glm::quat& rotation);
    void setScale(EntityHandle entity, const glm::vec3& scale);
    void translate(EntityHandle entity, const glm::vec3& delta);
    void setWorldMatrix(EntityHandle entity, const glm::mat4& world);
    [[nodiscard]] const glm::mat4& getLocalMatrix(EntityHandle entity) const;
    [[nodiscard]] glm::mat4 getWorldMatrix(EntityHandle entity) const;
    [[nodiscard]] inline glm::vec3 getPosition(EntityHandle entity) const { return valid(entity) ? m_position[entity.index] : glm::vec3(0.0f); }
    [[nodiscard]] glm::quat getRotation(EntityHandle entity) const;
    [[nodiscard]] glm::vec3 getScale(EntityHandle entity) const;
    [[nodiscard]] glm::vec3 getWorldPosition(EntityHandle entity) const;
    [[nodiscard]] glm::quat getWorldRotation(EntityHandle entity) const;
    [[nodiscard]] glm::vec3 forward(EntityHandle entity) const;
    [[nodiscard]] glm::vec3 right(EntityHandle entity) const;
    [[nodiscard]] glm::vec3 up(EntityHandle entity) const;
    void setWorldPosition(EntityHandle entity, const glm::vec3& position);
    void setWorldRotation(EntityHandle entity, const glm::quat& rotation);
    void rotate(EntityHandle entity, const glm::vec3& axis, float radians);
    void lookAt(EntityHandle entity, const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f));

    void setLayer(EntityHandle entity, uint32_t mask);
    [[nodiscard]] uint32_t getLayer(EntityHandle entity) const;
    void forEachInLayer(uint32_t mask, const std::function<void(EntityHandle)>& fn) const;
    void setTag(EntityHandle entity, std::string tag);
    [[nodiscard]] const std::string& getTag(EntityHandle entity) const;
    [[nodiscard]] std::vector<EntityHandle> findByTag(const std::string& tag) const;

    void setRenderFlags(EntityHandle entity, uint32_t flags, bool enabled);
    [[nodiscard]] uint32_t getRenderFlags(EntityHandle entity) const;
    void setVisible(EntityHandle entity, bool visible);
    [[nodiscard]] bool isVisible(EntityHandle entity) const;
    [[nodiscard]] uint32_t getMesh(EntityHandle entity) const;
    void setMesh(EntityHandle entity, uint32_t mesh);
    void setMaterial(EntityHandle entity, uint32_t material);
    void setShader(EntityHandle entity, uint32_t shader);
    void setAlpha(EntityHandle entity, float alpha);
    [[nodiscard]] const RenderableComponent& getRenderable(EntityHandle entity) const;

    [[nodiscard]] std::vector<std::string> validate();
    [[nodiscard]] bool isValid() { return validate().empty(); }

    [[nodiscard]] EntityDescription describeEntity(EntityHandle entity) const;
    [[nodiscard]] std::vector<std::string> componentNames() const;
    bool setComponentFromText(EntityHandle entity, const std::string& componentName, const std::string& text);
    bool removeComponentByName(EntityHandle entity, const std::string& componentName);
    bool addComponentByName(EntityHandle entity, const std::string& componentName);
    [[nodiscard]] std::optional<std::string> componentText(EntityHandle entity, const std::string& componentName) const;

    Script* addScript(EntityHandle entity, std::unique_ptr<Script> script);
    template <typename T, typename... Args>
    T* attach(EntityHandle entity, Args&&... args) {
        return static_cast<T*>(addScript(entity, std::make_unique<T>(std::forward<Args>(args)...)));
    }
    template <typename T>
    [[nodiscard]] T* getScript(EntityHandle entity) const {
        if (!valid(entity)) return nullptr;
        const auto it = m_scripts.find(entity.index);
        if (it == m_scripts.end()) return nullptr;
        for (const auto& script : it->second) {
            if (auto* typed = dynamic_cast<T*>(script.get())) return typed;
        }
        return nullptr;
    }
    void removeScripts(EntityHandle entity);

    template <typename T, typename... Args>
    T* add(EntityHandle entity, Args&&... args) {
        if (!valid(entity)) return nullptr;
        return &pool<T>().emplace(entity.index, std::forward<Args>(args)...);
    }
    template <typename T>
    [[nodiscard]] T* get(EntityHandle entity) {
        return valid(entity) ? pool<T>().find(entity.index) : nullptr;
    }
    template <typename T>
    [[nodiscard]] bool has(EntityHandle entity) {
        return get<T>(entity) != nullptr;
    }
    template <typename T>
    void remove(EntityHandle entity) {
        if (valid(entity)) pool<T>().remove(entity.index);
    }
    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        static_assert(sizeof...(Ts) >= 1);
        auto pools = std::forward_as_tuple(pool<Ts>()...);
        const std::vector<Entity>* owners[] = {&pool<Ts>().owners()...};
        size_t smallest = 0;
        for (size_t candidate = 1; candidate < sizeof...(Ts); ++candidate) {
            if (owners[candidate]->size() < owners[smallest]->size()) smallest = candidate;
        }
        for (const Entity entity : *owners[smallest]) {
            const bool all = std::apply([entity](auto&... pools) { return ((pools.find(entity) != nullptr) && ...); }, pools);
            if (!all) continue;
            std::apply([&](auto&... pools) { fn(EntityHandle{entity, m_generation[entity]}, *pools.find(entity)...); }, pools);
        }
    }

    [[nodiscard]] EntityBuilder spawn();

    template <typename T, typename Fn>
    void view(Fn&& fn) {
        auto& componentPool = pool<T>();
        for (size_t slot = 0; slot < componentPool.owners().size(); ++slot) {
            const Entity owner = componentPool.owners()[slot];
            fn(EntityHandle{owner, m_generation[owner]}, componentPool.data()[slot]);
        }
    }
    void update(float deltaTime);
    using TimerFn = std::function<void(World&, EntityHandle)>;
    uint32_t after(float seconds, TimerFn fn, EntityHandle owner = NULL_ENTITY);
    uint32_t every(float seconds, TimerFn fn, EntityHandle owner = NULL_ENTITY);
    bool cancel(uint32_t timerId);
    [[nodiscard]] size_t timerCount() const { return m_timers.size(); }
    void fixedUpdate(float fixedDelta);

    template <typename T, typename Save, typename Load>
    void registerComponent(std::string name, Save save, Load load) {
        ComponentSerializer serializer{
            [save](const IComponentPool& componentPool, std::ostream& out, Entity entity) {
                if (const T* value = static_cast<const ComponentPool<T>&>(componentPool).find(entity)) save(*value, out);
            },
            [load](World& world, EntityHandle entity, std::istream& in, const LoadContext& context) {
                std::optional<T> value;
                if constexpr (std::is_invocable_v<Load, std::istream&, const LoadContext&>) {
                    value = load(in, context);
                } else {
                    value = load(in);
                }
                if (!value) return false;
                world.add<T>(entity, std::move(*value));
                return true;
            },
            [](const IComponentPool& componentPool, const std::function<void(Entity)>& visitor) {
                for (Entity entity : static_cast<const ComponentPool<T>&>(componentPool).owners()) visitor(entity);
            },
            std::type_index(typeid(T)),
            {}};
        if constexpr (std::is_default_constructible_v<T>) {
            serializer.create = [](World& world, EntityHandle entity) { return world.add<T>(entity) != nullptr; };
        }
        m_serializers.insert_or_assign(std::move(name), std::move(serializer));
    }

    void setMeshHooks(std::function<std::string(uint32_t)> nameOf, std::function<uint32_t(const std::string&)> load) {
        m_meshNameOf = std::move(nameOf);
        m_meshLoad = std::move(load);
    }

    [[nodiscard]] Prefab capture(EntityHandle root) const;
    EntityHandle instantiate(const Prefab& prefab, EntityHandle parent = NULL_ENTITY, std::vector<EntityHandle>* createdOut = nullptr);

    void setMeshBoundsHook(std::function<glm::vec4(uint32_t)> bounds) { m_meshBounds = std::move(bounds); }
    void setAnimation(uint32_t firstEntity, std::vector<glm::vec3> basePositions);
    void setAnimationTime(float time) { m_animTime = time; }
    [[nodiscard]] static glm::vec3 orbitOffset(float time, uint32_t animIndex);
    void setJobSystem(JobSystem* jobs) { m_jobs = jobs; }
    void setProfiler(Profiler* profiler) { m_profiler = profiler; }
    [[nodiscard]] Profiler* profiler() const { return m_profiler; }
    void setSpatialCellSize(float size);
    [[nodiscard]] std::vector<EntityHandle> queryFrustum(const Camera& camera);
    void queryFrustum(const Camera& camera, std::vector<EntityHandle>& out);
    [[nodiscard]] std::optional<glm::vec4> sceneBounds();
    [[nodiscard]] glm::vec4 getWorldBounds(EntityHandle entity) const;
    [[nodiscard]] Ray screenRay(float screenX, float screenY, float width, float height) const;
    [[nodiscard]] std::vector<EntityHandle> overlapSphere(const glm::vec3& center, float radius);
    void overlapSphere(const glm::vec3& center, float radius, std::vector<EntityHandle>& out);
    [[nodiscard]] std::optional<RayHit> raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1.0e30f);

    template <typename T, typename Save, typename Load>
    void registerScript(std::string name, Save save, Load load) {
        ScriptSerializer serializer{
            [save](const Script& script, std::ostream& out) {
                const T* typed = dynamic_cast<const T*>(&script);
                if (!typed || typeid(script) != typeid(T)) return false;
                save(*typed, out);
                return true;
            },
            [load](World& world, EntityHandle entity, std::istream& in, const LoadContext& context) {
                std::optional<T> value;
                if constexpr (std::is_invocable_v<Load, std::istream&, const LoadContext&>) {
                    value = load(in, context);
                } else {
                    value = load(in);
                }
                if (!value) return false;
                world.attach<T>(entity, std::move(*value));
                return true;
            }};
        m_scriptNames.insert_or_assign(std::type_index(typeid(T)), name);
        m_scriptSerializers.insert_or_assign(std::move(name), std::move(serializer));
    }

    using SceneMigration = std::function<void(int fileVersion, std::string& line)>;
    void setSceneMigration(SceneMigration migration) { m_sceneMigration = std::move(migration); }
    void setRestoreCameraOnLoad(bool enabled) { m_restoreCameraOnLoad = enabled; }
    void addSceneSection(std::string keyword, std::function<void(std::ostream&)> write, std::function<void(std::string_view)> read, std::function<void()> begin = {}, std::function<void()> end = {}) {
        m_sceneSections.push_back({std::move(keyword), std::move(write), std::move(read), std::move(begin), std::move(end)});
    }
    static constexpr int kSceneVersion = 3;
    static constexpr size_t kMaxSceneEntities = 50000000;
    static constexpr size_t kMaxSceneLineLength = 65536;
    static constexpr size_t kMaxNameLength = 4096;

    void clear();
    bool saveScene(const std::filesystem::path& path) const;
    bool saveScene(std::ostream& out) const;
    bool loadScene(std::istream& in);
    [[nodiscard]] std::string snapshot() const;
    [[nodiscard]] WorldSnapshot captureSnapshot() const;
    void restoreSnapshot(const WorldSnapshot& snapshot);
    bool restore(const std::string& snapshotText, std::vector<EntityHandle>* remap = nullptr);
    bool loadScene(const std::filesystem::path& path);
    std::optional<std::vector<EntityHandle>> loadSceneAdditive(const std::filesystem::path& path, EntityHandle parent = NULL_ENTITY);
    void unloadGroup(const std::vector<EntityHandle>& roots);

    void forEach(const std::function<void(EntityHandle)>& fn) const;

    void setActiveCamera(EntityHandle entity) { m_activeCamera = entity; }
    [[nodiscard]] EntityHandle getActiveCamera() const { return valid(m_activeCamera) ? m_activeCamera : NULL_ENTITY; }
    void syncCamera();

    [[nodiscard]] const TimeState& time() const { return m_time; }
    [[nodiscard]] TimeState& timeState() { return m_time; }

    [[nodiscard]] EventBus& events() { return m_events; }
    [[nodiscard]] RandomService& random() { return m_random; }
    [[nodiscard]] const RandomService& random() const { return m_random; }
    [[nodiscard]] Camera& camera() { return m_camera; }
    [[nodiscard]] const Camera& camera() const { return m_camera; }
    [[nodiscard]] SceneDatabase& database() { return m_db; }
    [[nodiscard]] const SceneDatabase& database() const { return m_db; }

  private:
    struct SpatialBatch;
    struct SceneLoader;
    struct Validator;

    void destroyRecursive(Entity index);
    void shrinkSlots(size_t count);
    void trimTail();
    void moveEntity(Entity from, Entity to);
    template <typename T>
    ComponentPool<T>& pool() {
        auto& slot = m_pools[std::type_index(typeid(T))];
        if (!slot) slot = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*slot);
    }
    void runScripts(const std::function<void(Script&, EntityHandle)>& fn);
    void runTimers(float deltaTime);
    void runScriptDestroy(Entity index);
    void flushPendingDestroy();
    EntityHandle createImpl(const EntityDesc& desc);
    void writeExtras(std::ostream& out) const;
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> serializeScripts(Entity index) const;
    void applyExtraLines(const std::vector<std::string>& lines, const LoadContext& context);
    std::optional<std::vector<EntityHandle>> loadSceneText(std::string_view text, bool additive, EntityHandle parent, bool trusted, std::vector<EntityHandle>* remap);
    std::optional<std::vector<EntityHandle>> loadSceneImpl(std::istream& in, bool additive, EntityHandle parent, bool trusted = false, std::vector<EntityHandle>* remap = nullptr);
    bool claimSectionLine(const std::string& line, std::vector<std::pair<size_t, std::string>>& sectionLines) const;
    void applySceneSections(const std::vector<std::pair<size_t, std::string>>& sectionLines);
    void applyCameraValues(const std::array<float, 8>& values);
    void appendEntityLine(std::string& line, Entity index) const;
    void appendEntityLines(std::string& out) const;
    void appendMeshTable(std::string& out) const;
    void appendSections(std::string& out) const;
    void appendTags(std::string& out) const;
    void appendScene(std::string& out) const;
    void restoreSnapshotArrays(const WorldSnapshot& snapshot);
    void restoreSnapshotExtras(const std::string& extras);
    void clearTag(Entity index);
    void setFlagBit(Entity index, uint32_t bit, bool on);
    void link(Entity index, Entity parent);
    void unlink(Entity index);
    [[nodiscard]] inline bool valid(EntityHandle handle) const { return handle.index < m_alive.size() && m_alive[handle.index] && m_generation[handle.index] == handle.generation; }
    void touchTransform(Entity index);
    void syncPosition(Entity index) { m_position[index] = glm::vec3(m_db.transforms[index].localMatrix[3]); }
    void rebuildPositions();
    void invalidateWorld(Entity index) const;
    void syncAnimationCache() const;
    [[nodiscard]] glm::mat4 effectiveLocal(Entity index) const;
    [[nodiscard]] glm::mat4 worldNoCache(Entity index) const;
    [[nodiscard]] bool isAnimated(Entity index) const;
    [[nodiscard]] glm::mat4 sweptWorld(Entity index, float& sweep) const;
    void computeSpatialEntry(Entity index, SpatialEntry& out, bool swept) const;
    [[nodiscard]] SpatialEntry liveSpatial(Entity index) const;
    [[nodiscard]] const SpatialEntry& spatialView(Entity index, SpatialEntry& scratch) const;
    void insertSpatialEntry(Entity index);
    void queueSpatial(Entity index);
    void markSpatialSubtree(Entity index);
    void expandSpatialRoots();
    void refreshSpatial();
    void buildSpatialIndex();
    void applySpatialUpdatesSerial(const std::vector<Entity>& pending, const std::vector<SpatialEntry>& fresh);
    bool refreshSpatialInPlace(SpatialEntry& entry, const SpatialEntry& next);
    void applySpatialUpdatesParallel(const std::vector<Entity>& pending, const std::vector<SpatialEntry>& fresh);
    template <typename Filter, typename Visitor>
    void visitCells(const std::array<int, 6>& bounds, Filter&& filter, Visitor&& visitor) const;
    [[nodiscard]] const std::vector<Entity>* cellBucket(int cellX, int cellY, int cellZ) const;
    void removeSpatialEntry(Entity index);
    void noteCellAdded(uint64_t key);
    void noteCellRemoved(uint64_t key);
    [[nodiscard]] bool coarseBlockEmpty(int coarseX, int coarseY, int coarseZ) const;
    void growCellBounds(uint64_t key);
    void resetCellBounds();
    [[nodiscard]] uint64_t spatialKey(int cellX, int cellY, int cellZ) const;
    void touchStructure();
    void touchRenderable(Entity index);
    void relevelSubtree(Entity root);

    SceneDatabase m_db;
    Camera m_camera;
    std::vector<SpatialEntry> m_spatial;
    std::vector<uint8_t> m_spatialQueued;
    std::vector<Entity> m_spatialPending;
    std::vector<Entity> m_spatialRoots;
    std::vector<uint8_t> m_spatialRootQueued;
    std::vector<Entity> m_spatialStack;
    std::unordered_map<uint64_t, std::vector<Entity>> m_cells;
    std::unordered_map<uint64_t, uint32_t> m_coarseCells;
    std::vector<Entity> m_largeEntities;
    std::function<glm::vec4(uint32_t)> m_meshBounds;
    float m_cellSize = 16.0f;
    float m_maxSmallRadius = 0.0f;
    std::array<int, 6> m_cellBounds = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), std::numeric_limits<int>::lowest(), std::numeric_limits<int>::lowest(), std::numeric_limits<int>::lowest()};
    bool m_spatialActive = false;
    JobSystem* m_jobs = nullptr;
    SceneMigration m_sceneMigration;
    struct SceneSection {
        std::string keyword;
        std::function<void(std::ostream&)> write;
        std::function<void(std::string_view)> read;
        std::function<void()> begin;
        std::function<void()> end;
    };
    std::vector<SceneSection> m_sceneSections;
    bool m_restoreCameraOnLoad = true;
    Profiler* m_profiler = nullptr;
    EventBus m_events;
    RandomService m_random;
    TimeState m_time;
    std::vector<glm::vec3> m_animBase;
    uint32_t m_animOffset = 0;
    float m_animTime = 0.0f;
    mutable float m_animCacheTime = std::numeric_limits<float>::quiet_NaN();
    EntityHandle m_activeCamera;
    std::vector<uint8_t> m_alive;
    std::vector<uint32_t> m_generation;
    std::vector<uint8_t> m_visible;
    std::vector<uint32_t> m_mesh;
    std::vector<uint32_t> m_layer;
    std::vector<glm::vec3> m_position;
    mutable std::vector<glm::mat4> m_worldCache;
    mutable std::vector<uint8_t> m_worldDirty;
    mutable std::vector<Entity> m_worldChain;
    mutable std::vector<Entity> m_worldStack;
    TextTable m_tags;
    std::unordered_map<std::string, std::vector<Entity>> m_tagIndex;
    std::vector<Entity> m_prevSibling;
    std::vector<Entity> m_relevelStack;
    Entity m_firstRoot = INVALID_ENTITY;
    uint32_t m_generationBase = 0;
    std::unordered_map<Entity, std::vector<std::unique_ptr<Script>>> m_scripts;
    std::vector<EntityHandle> m_pendingDestroy;
    std::vector<EntityHandle> m_pendingRemoveScripts;
    struct Timer {
        uint32_t id;
        double remaining;
        double interval;
        TimerFn fn;
        EntityHandle owner;
        bool cancelled = false;
    };
    std::vector<Timer> m_timers;
    uint32_t m_nextTimerId = 0;
    bool m_firingTimers = false;
    bool m_updating = false;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;
    std::map<std::string, ComponentSerializer> m_serializers;
    std::map<std::string, ScriptSerializer> m_scriptSerializers;
    std::unordered_map<std::type_index, std::string> m_scriptNames;
    std::function<std::string(uint32_t)> m_meshNameOf;
    std::function<uint32_t(const std::string&)> m_meshLoad;
    TextTable m_names;
    std::vector<Entity> m_freeList;
    size_t m_aliveCount = 0;
};
