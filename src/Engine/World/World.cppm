module;

#include <array>
#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <type_traits>
#include <ostream>
#include <utility>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <tuple>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

export module Engine.World;

import Engine.Render.entity;
import Engine.Events;
import Engine.Jobs;
import Engine.Profiler;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;

export struct CameraComponent {
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

export struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

export struct RayHit {
    EntityHandle entity;
    float distance = 0.0f;
};

export struct TimeState {
    float deltaTime = 0.0f;
    float unscaledDeltaTime = 0.0f;
    double elapsed = 0.0;
    double unscaledElapsed = 0.0;
    uint64_t frame = 0;
    float timeScale = 1.0f;
    bool paused = false;
};

export struct LightComponent {
    enum class Type : uint32_t { Directional, Point, Spot };
    Type type = Type::Point;
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float range = 100.0f;
    float specular = 1.0f;
    float innerConeDegrees = 20.0f;
    float outerConeDegrees = 30.0f;
};

export struct LightSet {
    std::array<glm::vec4, 2> directional{};
    std::vector<glm::vec4> packed;
    uint32_t count = 0;
};

export struct EntityDescription {
    std::string name;
    std::string tag;
    uint32_t layer = 1;
    uint32_t mesh = NO_MESH;
    bool visible = true;
    EntityHandle parent;
    size_t childCount = 0;
    std::vector<std::pair<std::string, std::string>> components;
    std::vector<std::pair<std::string, std::string>> scripts;
};

export struct EntityMoved {
    EntityHandle from;
    EntityHandle to;
};

export struct EntityCreated {
    EntityHandle entity;
};

export struct EntityDestroyed {
    EntityHandle entity;
};

export struct EntityReparented {
    EntityHandle entity;
    EntityHandle oldParent;
    EntityHandle newParent;
};

export struct EntityDesc {
    std::string name;
    uint32_t mesh = NO_MESH;
    uint32_t material = 0;
    uint32_t shader = 0;
    float alpha = 1.0f;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
    EntityHandle parent = NULL_ENTITY;
};

export struct PrefabNode {
    std::string name;
    uint32_t mesh = 0;
    uint32_t material = 0;
    uint32_t shader = 0;
    float alpha = 1.0f;
    bool visible = true;
    uint32_t layer = 1;
    uint32_t renderFlags = 0;
    std::string tag;
    glm::mat4 local{1.0f};
    int parent = -1;
    uint32_t sourceIndex = INVALID_ENTITY;
    uint32_t sourceGeneration = 0;
    std::vector<std::pair<std::string, std::string>> components;
    std::vector<std::pair<std::string, std::string>> scripts;
};

export struct Prefab {
    std::vector<PrefabNode> nodes;
    [[nodiscard]] bool empty() const { return nodes.empty(); }
};

export class World;

class IComponentPool {
  public:
    virtual ~IComponentPool() = default;
    virtual void remove(Entity e) = 0;
    virtual bool has(Entity e) const = 0;
    virtual void rename(Entity from, Entity to) = 0;
    virtual void clear() = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool {
  public:
    template <typename... Args>
    T& emplace(Entity e, Args&&... args) {
        if (e >= m_sparse.size()) m_sparse.resize(static_cast<size_t>(e) + 1, INVALID_ENTITY);
        if (m_sparse[e] != INVALID_ENTITY) {
            m_data[m_sparse[e]] = T(std::forward<Args>(args)...);
            return m_data[m_sparse[e]];
        }
        m_sparse[e] = static_cast<Entity>(m_data.size());
        m_owners.push_back(e);
        m_data.emplace_back(std::forward<Args>(args)...);
        return m_data.back();
    }

    [[nodiscard]] const T* find(Entity e) const {
        if (e >= m_sparse.size() || m_sparse[e] == INVALID_ENTITY) return nullptr;
        return &m_data[m_sparse[e]];
    }

    [[nodiscard]] T* find(Entity e) {
        if (e >= m_sparse.size() || m_sparse[e] == INVALID_ENTITY) return nullptr;
        return &m_data[m_sparse[e]];
    }

    void remove(Entity e) override {
        if (e >= m_sparse.size() || m_sparse[e] == INVALID_ENTITY) return;
        const Entity slot = m_sparse[e];
        const Entity last = m_owners.back();
        if (slot != m_data.size() - 1) {
            m_data[slot] = std::move(m_data.back());
            m_owners[slot] = last;
            m_sparse[last] = slot;
        }
        m_sparse[e] = INVALID_ENTITY;
        m_data.pop_back();
        m_owners.pop_back();
    }

    bool has(Entity e) const override { return e < m_sparse.size() && m_sparse[e] != INVALID_ENTITY; }

    void rename(Entity from, Entity to) override {
        if (from >= m_sparse.size() || m_sparse[from] == INVALID_ENTITY) return;
        if (to >= m_sparse.size()) m_sparse.resize(static_cast<size_t>(to) + 1, INVALID_ENTITY);
        const Entity slot = m_sparse[from];
        m_sparse[to] = slot;
        m_owners[slot] = to;
        m_sparse[from] = INVALID_ENTITY;
    }

    void clear() override {
        m_sparse.clear();
        m_owners.clear();
        m_data.clear();
    }

    [[nodiscard]] const std::vector<Entity>& owners() const { return m_owners; }
    [[nodiscard]] std::vector<T>& data() { return m_data; }

  private:
    std::vector<Entity> m_sparse;
    std::vector<Entity> m_owners;
    std::vector<T> m_data;
};

export class Script {
  public:
    virtual ~Script() = default;
    virtual void onStart(World&, EntityHandle) {}
    virtual void onUpdate(World&, EntityHandle, float) {}
    virtual void onFixedUpdate(World&, EntityHandle, float) {}
    virtual void onDestroy(World&, EntityHandle) {}

  private:
    friend class World;
    bool m_started = false;
};

export struct LoadContext {
    std::function<EntityHandle(uint32_t)> resolveFn;
    [[nodiscard]] EntityHandle resolve(uint32_t savedIndex) const { return resolveFn ? resolveFn(savedIndex) : NULL_ENTITY; }
};

struct ScriptSerializer {
    std::function<bool(const Script&, std::ostream&)> write;
    std::function<bool(World&, EntityHandle, std::istream&, const LoadContext&)> read;
};

struct ComponentSerializer {
    std::function<void(const IComponentPool&, std::ostream&, Entity)> write;
    std::function<bool(World&, EntityHandle, std::istream&, const LoadContext&)> read;
    std::function<void(const IComponentPool&, const std::function<void(Entity)>&)> owners;
    std::type_index type;
};

export class EntityBuilder;

struct SpatialEntry {
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    uint64_t cell = 0;
    uint8_t state = 0;
};

export class World {
  public:
    World();

    EntityHandle create(const EntityDesc& desc = {});
    EntityHandle create(std::string name, uint32_t mesh = NO_MESH, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), EntityHandle parent = NULL_ENTITY);
    template <typename Fn>
    void createBatch(size_t count, Fn&& fn) {
        reserve(m_alive.size() + count);
        EntityDesc desc;
        for (size_t i = 0; i < count; ++i) {
            desc = EntityDesc{};
            fn(i, desc);
            createImpl(desc);
        }
        touchStructure();
    }
    void destroy(EntityHandle e);
    void destroyBatch(std::span<const EntityHandle> entities);
    void setPositions(std::span<const std::pair<EntityHandle, glm::vec3>> updates);
    std::vector<EntityMoved> compact();
    [[nodiscard]] bool isAlive(EntityHandle e) const;
    [[nodiscard]] size_t aliveCount() const { return m_aliveCount; }
    void reserve(size_t count);
    [[nodiscard]] EntityHandle handleOf(Entity index) const;
    [[nodiscard]] static Entity slot(EntityHandle h) { return h.index; }

    void setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform = true);
    [[nodiscard]] EntityHandle getParent(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> getChildren(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> getRoots() const;
    [[nodiscard]] EntityHandle firstRoot() const { return handleOf(m_firstRoot); }
    [[nodiscard]] EntityHandle firstChild(EntityHandle e) const { return valid(e) ? handleOf(m_firstChild[e.index]) : NULL_ENTITY; }
    [[nodiscard]] EntityHandle nextSibling(EntityHandle e) const { return valid(e) ? handleOf(m_nextSibling[e.index]) : NULL_ENTITY; }
    [[nodiscard]] bool isDescendantOf(EntityHandle e, EntityHandle ancestor) const;

    void setName(EntityHandle e, std::string name);
    [[nodiscard]] const std::string& getName(EntityHandle e) const;
    [[nodiscard]] EntityHandle find(std::string_view name) const;
    [[nodiscard]] std::vector<EntityHandle> findAll(std::string_view name) const;

    void setLocalMatrix(EntityHandle e, const glm::mat4& local);
    void setPosition(EntityHandle e, const glm::vec3& position);
    void setRotation(EntityHandle e, const glm::quat& rotation);
    void setScale(EntityHandle e, const glm::vec3& scale);
    void translate(EntityHandle e, const glm::vec3& delta);
    void setWorldMatrix(EntityHandle e, const glm::mat4& world);
    [[nodiscard]] const glm::mat4& getLocalMatrix(EntityHandle e) const;
    [[nodiscard]] glm::mat4 getWorldMatrix(EntityHandle e) const;
    [[nodiscard]] glm::vec3 getPosition(EntityHandle e) const;
    [[nodiscard]] glm::quat getRotation(EntityHandle e) const;
    [[nodiscard]] glm::vec3 getScale(EntityHandle e) const;
    [[nodiscard]] glm::vec3 getWorldPosition(EntityHandle e) const;
    [[nodiscard]] glm::quat getWorldRotation(EntityHandle e) const;
    [[nodiscard]] glm::vec3 forward(EntityHandle e) const;
    [[nodiscard]] glm::vec3 right(EntityHandle e) const;
    [[nodiscard]] glm::vec3 up(EntityHandle e) const;
    void setWorldPosition(EntityHandle e, const glm::vec3& position);
    void setWorldRotation(EntityHandle e, const glm::quat& rotation);
    void rotate(EntityHandle e, const glm::vec3& axis, float radians);
    void lookAt(EntityHandle e, const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f));

    void setLayer(EntityHandle e, uint32_t mask);
    [[nodiscard]] uint32_t getLayer(EntityHandle e) const;
    void forEachInLayer(uint32_t mask, const std::function<void(EntityHandle)>& fn) const;
    void setTag(EntityHandle e, std::string tag);
    [[nodiscard]] const std::string& getTag(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> findByTag(const std::string& tag) const;

    void setRenderFlags(EntityHandle e, uint32_t flags, bool enabled);
    [[nodiscard]] uint32_t getRenderFlags(EntityHandle e) const;
    void setVisible(EntityHandle e, bool visible);
    [[nodiscard]] bool isVisible(EntityHandle e) const;
    [[nodiscard]] uint32_t getMesh(EntityHandle e) const;
    void setMesh(EntityHandle e, uint32_t mesh);
    void setMaterial(EntityHandle e, uint32_t material);
    void setShader(EntityHandle e, uint32_t shader);
    void setAlpha(EntityHandle e, float alpha);
    [[nodiscard]] const RenderableComponent& getRenderable(EntityHandle e) const;

    [[nodiscard]] EntityDescription describeEntity(EntityHandle e) const;
    [[nodiscard]] std::vector<std::string> componentNames() const;
    bool setComponentFromText(EntityHandle e, const std::string& componentName, const std::string& text);
    bool removeComponentByName(EntityHandle e, const std::string& componentName);
    [[nodiscard]] std::optional<std::string> componentText(EntityHandle e, const std::string& componentName) const;

    Script* addScript(EntityHandle e, std::unique_ptr<Script> script);
    template <typename T, typename... Args>
    T* attach(EntityHandle e, Args&&... args) {
        return static_cast<T*>(addScript(e, std::make_unique<T>(std::forward<Args>(args)...)));
    }
    template <typename T>
    [[nodiscard]] T* getScript(EntityHandle e) const {
        if (!valid(e)) return nullptr;
        const auto it = m_scripts.find(e.index);
        if (it == m_scripts.end()) return nullptr;
        for (const auto& s : it->second) {
            if (auto* p = dynamic_cast<T*>(s.get())) return p;
        }
        return nullptr;
    }
    void removeScripts(EntityHandle e);

    template <typename T, typename... Args>
    T* add(EntityHandle e, Args&&... args) {
        if (!valid(e)) return nullptr;
        return &pool<T>().emplace(e.index, std::forward<Args>(args)...);
    }
    template <typename T>
    [[nodiscard]] T* get(EntityHandle e) {
        return valid(e) ? pool<T>().find(e.index) : nullptr;
    }
    template <typename T>
    [[nodiscard]] bool has(EntityHandle e) {
        return get<T>(e) != nullptr;
    }
    template <typename T>
    void remove(EntityHandle e) {
        if (valid(e)) pool<T>().remove(e.index);
    }
    template <typename... Ts, typename Fn>
    void each(Fn&& fn) {
        static_assert(sizeof...(Ts) >= 1);
        auto pools = std::forward_as_tuple(pool<Ts>()...);
        const std::vector<Entity>* owners[] = {&pool<Ts>().owners()...};
        size_t smallest = 0;
        for (size_t i = 1; i < sizeof...(Ts); ++i) {
            if (owners[i]->size() < owners[smallest]->size()) smallest = i;
        }
        for (const Entity e : *owners[smallest]) {
            const bool all = std::apply([e](auto&... p) { return ((p.find(e) != nullptr) && ...); }, pools);
            if (!all) continue;
            std::apply([&](auto&... p) { fn(EntityHandle{e, m_generation[e]}, *p.find(e)...); }, pools);
        }
    }

    [[nodiscard]] EntityBuilder spawn();

    template <typename T, typename Fn>
    void view(Fn&& fn) {
        auto& p = pool<T>();
        for (size_t i = 0; i < p.owners().size(); ++i) fn(EntityHandle{p.owners()[i], m_generation[p.owners()[i]]}, p.data()[i]);
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
            [save](const IComponentPool& p, std::ostream& out, Entity e) {
                if (const T* value = static_cast<const ComponentPool<T>&>(p).find(e)) save(*value, out);
            },
            [load](World& w, EntityHandle e, std::istream& in, const LoadContext& ctx) {
                std::optional<T> value;
                if constexpr (std::is_invocable_v<Load, std::istream&, const LoadContext&>) {
                    value = load(in, ctx);
                } else {
                    value = load(in);
                }
                if (!value) return false;
                w.add<T>(e, std::move(*value));
                return true;
            },
            [](const IComponentPool& p, const std::function<void(Entity)>& fn) {
                for (Entity e : static_cast<const ComponentPool<T>&>(p).owners()) fn(e);
            },
            std::type_index(typeid(T))};
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
    [[nodiscard]] std::optional<glm::vec4> sceneBounds();
    [[nodiscard]] glm::vec4 getWorldBounds(EntityHandle e) const;
    [[nodiscard]] Ray screenRay(float screenX, float screenY, float width, float height) const;
    [[nodiscard]] std::vector<EntityHandle> overlapSphere(const glm::vec3& center, float radius);
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
            [load](World& w, EntityHandle e, std::istream& in, const LoadContext& ctx) {
                std::optional<T> value;
                if constexpr (std::is_invocable_v<Load, std::istream&, const LoadContext&>) {
                    value = load(in, ctx);
                } else {
                    value = load(in);
                }
                if (!value) return false;
                w.attach<T>(e, std::move(*value));
                return true;
            }};
        m_scriptNames.insert_or_assign(std::type_index(typeid(T)), name);
        m_scriptSerializers.insert_or_assign(std::move(name), std::move(serializer));
    }

    using SceneMigration = std::function<void(int fileVersion, std::string& line)>;
    void setSceneMigration(SceneMigration migration) { m_sceneMigration = std::move(migration); }
    void setRestoreCameraOnLoad(bool enabled) { m_restoreCameraOnLoad = enabled; }
    static constexpr int kSceneVersion = 3;

    void clear();
    bool saveScene(const std::filesystem::path& path) const;
    bool saveScene(std::ostream& out) const;
    bool loadScene(std::istream& in);
    [[nodiscard]] std::string snapshot() const;
    bool restore(const std::string& snapshotText);
    bool loadScene(const std::filesystem::path& path);
    std::optional<std::vector<EntityHandle>> loadSceneAdditive(const std::filesystem::path& path, EntityHandle parent = NULL_ENTITY);
    void unloadGroup(const std::vector<EntityHandle>& roots);

    void forEach(const std::function<void(EntityHandle)>& fn) const;

    void setActiveCamera(EntityHandle e) { m_activeCamera = e; }
    [[nodiscard]] EntityHandle getActiveCamera() const { return valid(m_activeCamera) ? m_activeCamera : NULL_ENTITY; }
    void syncCamera();

    [[nodiscard]] const TimeState& time() const { return m_time; }
    [[nodiscard]] TimeState& timeState() { return m_time; }

    [[nodiscard]] EventBus& events() { return m_events; }
    [[nodiscard]] Camera& camera() { return m_camera; }
    [[nodiscard]] const Camera& camera() const { return m_camera; }
    [[nodiscard]] SceneDatabase& database() { return m_db; }
    [[nodiscard]] const SceneDatabase& database() const { return m_db; }

  private:
    void destroyRecursive(Entity idx);
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
    void runScriptDestroy(Entity idx);
    void flushPendingDestroy();
    EntityHandle createImpl(const EntityDesc& desc);
    std::optional<std::vector<EntityHandle>> loadSceneImpl(std::istream& in, bool additive, EntityHandle parent);
    void clearTag(Entity idx);
    void setFlagBit(Entity idx, uint32_t bit, bool on);
    void link(Entity idx, Entity parent);
    void unlink(Entity idx);
    [[nodiscard]] bool valid(EntityHandle h) const;
    void touchTransform(Entity idx);
    void invalidateWorld(Entity idx) const;
    void syncAnimationCache() const;
    void syncAnimationSpatial();
    [[nodiscard]] glm::mat4 effectiveLocal(Entity idx) const;
    [[nodiscard]] glm::mat4 worldNoCache(Entity idx) const;
    void computeSpatialEntry(Entity idx, SpatialEntry& out) const;
    void queueSpatial(Entity idx);
    void markSpatialSubtree(Entity idx);
    void refreshSpatial();
    void rebuildSpatialEntry(Entity idx);
    void removeSpatialEntry(Entity idx);
    [[nodiscard]] uint64_t spatialKey(int x, int y, int z) const;
    void touchStructure();
    void touchData();

    SceneDatabase m_db;
    Camera m_camera;
    std::vector<SpatialEntry> m_spatial;
    std::vector<uint8_t> m_spatialQueued;
    std::vector<Entity> m_spatialPending;
    std::vector<Entity> m_spatialStack;
    std::unordered_map<uint64_t, std::vector<Entity>> m_cells;
    std::vector<Entity> m_largeEntities;
    std::function<glm::vec4(uint32_t)> m_meshBounds;
    float m_cellSize = 16.0f;
    float m_maxSmallRadius = 0.0f;
    bool m_spatialActive = false;
    JobSystem* m_jobs = nullptr;
    SceneMigration m_sceneMigration;
    bool m_restoreCameraOnLoad = true;
    Profiler* m_profiler = nullptr;
    EventBus m_events;
    TimeState m_time;
    std::vector<glm::vec3> m_animBase;
    uint32_t m_animOffset = 0;
    float m_animTime = 0.0f;
    mutable float m_animCacheTime = std::numeric_limits<float>::quiet_NaN();
    float m_animSpatialTime = std::numeric_limits<float>::quiet_NaN();
    EntityHandle m_activeCamera;
    std::vector<uint8_t> m_alive;
    std::vector<uint32_t> m_generation;
    std::vector<uint8_t> m_visible;
    std::vector<uint32_t> m_mesh;
    std::vector<uint32_t> m_layer;
    mutable std::vector<glm::mat4> m_worldCache;
    mutable std::vector<uint8_t> m_worldDirty;
    mutable std::vector<Entity> m_worldChain;
    mutable std::vector<Entity> m_worldStack;
    std::vector<std::string> m_tags;
    std::unordered_map<std::string, std::vector<Entity>> m_tagIndex;
    std::vector<Entity> m_firstChild;
    std::vector<Entity> m_nextSibling;
    std::vector<Entity> m_prevSibling;
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
    std::vector<std::string> m_names;
    std::vector<Entity> m_freeList;
    size_t m_aliveCount = 0;
};


export bool collectLights(World& world, const glm::vec3& viewPos, LightSet& out, size_t maxLights = 64);


export class EntityBuilder {
  public:
    explicit EntityBuilder(World& world) : m_world(world) {}

    EntityBuilder& name(std::string value) {
        m_desc.name = std::move(value);
        return *this;
    }
    EntityBuilder& mesh(uint32_t value) {
        m_desc.mesh = value;
        return *this;
    }
    EntityBuilder& material(uint32_t value) {
        m_desc.material = value;
        return *this;
    }
    EntityBuilder& shader(uint32_t value) {
        m_desc.shader = value;
        return *this;
    }
    EntityBuilder& alpha(float value) {
        m_desc.alpha = value;
        return *this;
    }
    EntityBuilder& at(const glm::vec3& value) {
        m_desc.position = value;
        return *this;
    }
    EntityBuilder& rotation(const glm::quat& value) {
        m_desc.rotation = value;
        return *this;
    }
    EntityBuilder& scale(const glm::vec3& value) {
        m_desc.scale = value;
        return *this;
    }
    EntityBuilder& parent(EntityHandle value) {
        m_desc.parent = value;
        return *this;
    }
    EntityBuilder& layer(uint32_t value) {
        m_layer = value;
        return *this;
    }
    EntityBuilder& tag(std::string value) {
        m_tag = std::move(value);
        return *this;
    }
    EntityBuilder& visible(bool value) {
        m_visible = value;
        return *this;
    }

    template <typename T, typename... Args>
    EntityBuilder& with(Args... args) {
        m_steps.push_back([... captured = std::move(args)](World& world, EntityHandle e) mutable { world.add<T>(e, captured...); });
        return *this;
    }

    EntityHandle build() {
        const EntityHandle e = m_world.create(m_desc);
        if (m_layer) m_world.setLayer(e, *m_layer);
        if (!m_tag.empty()) m_world.setTag(e, m_tag);
        if (!m_visible) m_world.setVisible(e, false);
        for (auto& step : m_steps) step(m_world, e);
        return e;
    }

    operator EntityHandle() { return build(); }

  private:
    World& m_world;
    EntityDesc m_desc;
    std::optional<uint32_t> m_layer;
    std::string m_tag;
    bool m_visible = true;
    std::vector<std::function<void(World&, EntityHandle)>> m_steps;
};

inline EntityBuilder World::spawn() { return EntityBuilder(*this); }
