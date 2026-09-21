module;

#include <cstdint>

module Sandbox.scene;

import Engine.Render.component;
import Engine.glm;

void Scene::onStart() {
    const glm::vec3 size(0.5f, 1.5f, 0.5f);
    const float gap = 0.2f;

    m_lower = m_ctx.world.create("lower", m_ctx.cubeMesh, glm::vec3(0.0f), size);
    m_upper = m_ctx.world.create("upper", m_ctx.cubeMesh, glm::vec3(0.0f, size.y + gap, 0.0f), size);

    m_ctx.world.camera().setPos(glm::vec3(1.5f, 0.6f, 5.0f));
}

void Scene::onUpdate(float deltaTime, float time) {
}
