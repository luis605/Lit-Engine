module;

#include <cstdint>
#include <functional>
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
    Entity parent = INVALID_ENTITY;
};

export class World {
  public:
    World();

    Entity create(const EntityDesc& desc = {});
    Entity create(std::string name, uint32_t mesh, const glm::vec3& position = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f), Entity parent = INVALID_ENTITY);
    void destroy(Entity e);
    [[nodiscard]] bool isAlive(Entity e) const;
    [[nodiscard]] size_t aliveCount() const { return m_aliveCount; }

    void setParent(Entity e, Entity parent, bool keepWorldTransform = true);
    [[nodiscard]] Entity getParent(Entity e) const;
    [[nodiscard]] std::vector<Entity> getChildren(Entity e) const;
    [[nodiscard]] std::vector<Entity> getRoots() const;
    [[nodiscard]] bool isDescendantOf(Entity e, Entity ancestor) const;

    void setName(Entity e, std::string name);
    [[nodiscard]] const std::string& getName(Entity e) const;
    [[nodiscard]] Entity find(std::string_view name) const;
    [[nodiscard]] std::vector<Entity> findAll(std::string_view name) const;

    void setLocalMatrix(Entity e, const glm::mat4& local);
    void setPosition(Entity e, const glm::vec3& position);
    void setRotation(Entity e, const glm::quat& rotation);
    void setScale(Entity e, const glm::vec3& scale);
    void translate(Entity e, const glm::vec3& delta);
    void setWorldMatrix(Entity e, const glm::mat4& world);
    [[nodiscard]] const glm::mat4& getLocalMatrix(Entity e) const;
    [[nodiscard]] glm::mat4 getWorldMatrix(Entity e) const;
    [[nodiscard]] glm::vec3 getPosition(Entity e) const;
    [[nodiscard]] glm::quat getRotation(Entity e) const;
    [[nodiscard]] glm::vec3 getScale(Entity e) const;
    [[nodiscard]] glm::vec3 getWorldPosition(Entity e) const;

    void setMesh(Entity e, uint32_t mesh);
    void setMaterial(Entity e, uint32_t material);
    void setShader(Entity e, uint32_t shader);
    void setAlpha(Entity e, float alpha);
    [[nodiscard]] const RenderableComponent& getRenderable(Entity e) const;

    void forEach(const std::function<void(Entity)>& fn) const;

    [[nodiscard]] Camera& camera() { return m_camera; }
    [[nodiscard]] const Camera& camera() const { return m_camera; }
    [[nodiscard]] SceneDatabase& database() { return m_db; }
    [[nodiscard]] const SceneDatabase& database() const { return m_db; }

  private:
    void destroyRecursive(Entity e);
    void touchTransform(Entity e);
    void touchStructure();
    void touchData();

    SceneDatabase m_db;
    Camera m_camera;
    std::vector<bool> m_alive;
    std::vector<std::string> m_names;
    std::vector<Entity> m_freeList;
    size_t m_aliveCount = 0;
};
