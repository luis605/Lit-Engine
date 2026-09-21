module;

#include <cstdint>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <ostream>
#include <utility>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

export module Engine.World;

import Engine.Render.entity;
import Engine.Events;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;

export struct CameraComponent {
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

export struct RayHit {
    EntityHandle entity;
    float distance = 0.0f;
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
    uint32_t mesh = 0;
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
    std::string tag;
    glm::mat4 local{1.0f};
    int parent = -1;
    std::vector<std::pair<std::string, std::string>> components;
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

struct ComponentSerializer {
    std::function<void(const IComponentPool&, std::ostream&, Entity)> write;
    std::function<bool(World&, EntityHandle, std::istream&)> read;
    std::function<void(const IComponentPool&, const std::function<void(Entity)>&)> owners;
    std::type_index type;
};

export class World {
  public:
    World();

    EntityHandle create(const EntityDesc& desc = {});
    EntityHandle create(std::string name, uint32_t mesh, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), EntityHandle parent = NULL_ENTITY);
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
    [[nodiscard]] bool isAlive(EntityHandle e) const;
    [[nodiscard]] size_t aliveCount() const { return m_aliveCount; }
    void reserve(size_t count);
    [[nodiscard]] EntityHandle handleOf(Entity index) const;
    [[nodiscard]] static Entity slot(EntityHandle h) { return h.index; }

    void setParent(EntityHandle e, EntityHandle parent, bool keepWorldTransform = true);
    [[nodiscard]] EntityHandle getParent(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> getChildren(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> getRoots() const;
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

    void setLayer(EntityHandle e, uint32_t mask);
    [[nodiscard]] uint32_t getLayer(EntityHandle e) const;
    void forEachInLayer(uint32_t mask, const std::function<void(EntityHandle)>& fn) const;
    void setTag(EntityHandle e, std::string tag);
    [[nodiscard]] const std::string& getTag(EntityHandle e) const;
    [[nodiscard]] std::vector<EntityHandle> findByTag(const std::string& tag) const;

    void setVisible(EntityHandle e, bool visible);
    [[nodiscard]] bool isVisible(EntityHandle e) const;
    [[nodiscard]] uint32_t getMesh(EntityHandle e) const;
    void setMesh(EntityHandle e, uint32_t mesh);
    void setMaterial(EntityHandle e, uint32_t material);
    void setShader(EntityHandle e, uint32_t shader);
    void setAlpha(EntityHandle e, float alpha);
    [[nodiscard]] const RenderableComponent& getRenderable(EntityHandle e) const;

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
    template <typename T, typename Fn>
    void view(Fn&& fn) {
        auto& p = pool<T>();
        for (size_t i = 0; i < p.owners().size(); ++i) fn(EntityHandle{p.owners()[i], m_generation[p.owners()[i]]}, p.data()[i]);
    }
    void update(float deltaTime);
    void fixedUpdate(float fixedDelta);

    template <typename T, typename Save, typename Load>
    void registerComponent(std::string name, Save save, Load load) {
        ComponentSerializer serializer{
            [save](const IComponentPool& p, std::ostream& out, Entity e) {
                if (const T* value = static_cast<const ComponentPool<T>&>(p).find(e)) save(*value, out);
            },
            [load](World& w, EntityHandle e, std::istream& in) {
                std::optional<T> value = load(in);
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
    EntityHandle instantiate(const Prefab& prefab, EntityHandle parent = NULL_ENTITY);

    void setMeshBoundsHook(std::function<glm::vec4(uint32_t)> bounds) { m_meshBounds = std::move(bounds); }
    void setSpatialCellSize(float size);
    [[nodiscard]] std::vector<EntityHandle> overlapSphere(const glm::vec3& center, float radius);
    [[nodiscard]] std::optional<RayHit> raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1.0e30f);

    void clear();
    bool saveScene(const std::filesystem::path& path) const;
    bool loadScene(const std::filesystem::path& path);

    void forEach(const std::function<void(EntityHandle)>& fn) const;

    void setActiveCamera(EntityHandle e) { m_activeCamera = e; }
    [[nodiscard]] EntityHandle getActiveCamera() const { return valid(m_activeCamera) ? m_activeCamera : NULL_ENTITY; }
    void syncCamera();

    [[nodiscard]] EventBus& events() { return m_events; }
    [[nodiscard]] Camera& camera() { return m_camera; }
    [[nodiscard]] const Camera& camera() const { return m_camera; }
    [[nodiscard]] SceneDatabase& database() { return m_db; }
    [[nodiscard]] const SceneDatabase& database() const { return m_db; }

  private:
    void destroyRecursive(Entity idx);
    template <typename T>
    ComponentPool<T>& pool() {
        auto& slot = m_pools[std::type_index(typeid(T))];
        if (!slot) slot = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*slot);
    }
    void runScripts(const std::function<void(Script&, EntityHandle)>& fn);
    void runScriptDestroy(Entity idx);
    void flushPendingDestroy();
    EntityHandle createImpl(const EntityDesc& desc);
    void clearTag(Entity idx);
    void link(Entity idx, Entity parent);
    void unlink(Entity idx);
    [[nodiscard]] bool valid(EntityHandle h) const;
    void touchTransform(Entity idx);
    void invalidateWorld(Entity idx) const;
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
    struct SpatialEntry {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
        uint64_t cell = 0;
        uint8_t state = 0;
    };
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
    EventBus m_events;
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
    bool m_updating = false;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> m_pools;
    std::map<std::string, ComponentSerializer> m_serializers;
    std::function<std::string(uint32_t)> m_meshNameOf;
    std::function<uint32_t(const std::string&)> m_meshLoad;
    std::vector<std::string> m_names;
    std::vector<Entity> m_freeList;
    size_t m_aliveCount = 0;
};
