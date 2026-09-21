module;

#include <cstdint>

export module Sandbox.scene;

import Engine.engine;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

export struct SceneContext {
    Engine& engine;
    World& world;
    uint32_t cubeMesh;
    uint32_t sphereMesh;
};

export class Scene {
  public:
    explicit Scene(SceneContext ctx) : m_ctx(ctx) {}

    void onStart();
    void onUpdate(float deltaTime, float time);

    SceneContext& getCntx() { return m_ctx; }
    EntityHandle lower() const { return m_lower; }
    EntityHandle upper() const { return m_upper; }

  private:
    SceneContext m_ctx;
    EntityHandle m_lower;
    EntityHandle m_upper;
};
