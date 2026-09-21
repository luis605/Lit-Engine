module;

#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

export module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;

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

export class World;

export class Script {
  public:
    virtual ~Script() = default;
    virtual void onStart(World&, EntityHandle) {}
    virtual void onUpdate(World&, EntityHandle, float) {}
    virtual void onDestroy(World&, EntityHandle) {}

  private:
    friend class World;
    bool m_started = false;
};

export class World {
  public:
    World();

    EntityHandle create(const EntityDesc& desc = {});
    EntityHandle create(std::string name, uint32_t mesh, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), EntityHandle parent = NULL_ENTITY);
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
    void update(float deltaTime);

    void clear();
    bool saveScene(const std::filesystem::path& path) const;
    bool loadScene(const std::filesystem::path& path);

    void forEach(const std::function<void(EntityHandle)>& fn) const;

    [[nodiscard]] Camera& camera() { return m_camera; }
    [[nodiscard]] const Camera& camera() const { return m_camera; }
    [[nodiscard]] SceneDatabase& database() { return m_db; }
    [[nodiscard]] const SceneDatabase& database() const { return m_db; }

  private:
    void destroyRecursive(Entity idx);
    void runScriptDestroy(Entity idx);
    void flushPendingDestroy();
    void link(Entity idx, Entity parent);
    void unlink(Entity idx);
    [[nodiscard]] bool valid(EntityHandle h) const;
    void touchTransform(Entity idx);
    void touchStructure();
    void touchData();

    SceneDatabase m_db;
    Camera m_camera;
    std::vector<uint8_t> m_alive;
    std::vector<uint32_t> m_generation;
    std::vector<uint8_t> m_visible;
    std::vector<uint32_t> m_mesh;
    std::vector<Entity> m_firstChild;
    std::vector<Entity> m_nextSibling;
    std::vector<Entity> m_prevSibling;
    Entity m_firstRoot = INVALID_ENTITY;
    uint32_t m_generationBase = 0;
    std::unordered_map<Entity, std::vector<std::unique_ptr<Script>>> m_scripts;
    std::vector<EntityHandle> m_pendingDestroy;
    std::vector<EntityHandle> m_pendingRemoveScripts;
    bool m_updating = false;
    std::vector<std::string> m_names;
    std::vector<Entity> m_freeList;
    size_t m_aliveCount = 0;
};
