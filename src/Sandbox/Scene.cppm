module;

#include <cstdint>

export module Sandbox.scene;

import Engine.engine;
import Engine.camera;
import Engine.Render.scenedatabase;
import Engine.Render.entity;
import Engine.glm;

export struct SceneContext {
    Engine& engine;
    SceneDatabase& scene;
    Camera& camera;
    uint32_t cubeMesh;
    uint32_t sphereMesh;
};

export class Scene {
  public:
    explicit Scene(SceneContext ctx) : m_ctx(ctx) {}

    void onStart();
    void onUpdate(float deltaTime, float time);

    SceneContext& getCntx() { return m_ctx; }

  private:
    Entity spawn(uint32_t mesh, const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), Entity parent = INVALID_ENTITY);
    void setLocal(Entity e, const glm::mat4& local);

    SceneContext m_ctx;
    Entity m_lower = INVALID_ENTITY;
    Entity m_upper = INVALID_ENTITY;
};
