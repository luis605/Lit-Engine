module;

#include <cstdint>
#include <new>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Sandbox.scene;

import Engine.Render.component;
import Engine.glm;

void Scene::onStart() {
    const glm::vec3 size(0.5f, 1.5f, 0.5f);
    const float gap = 0.2f;

    m_lower = m_ctx.world.create("lower", m_ctx.cubeMesh, glm::vec3(0.0f), size);
    m_upper = m_ctx.world.create("upper", m_ctx.cubeMesh, glm::vec3(0.0f, size.y + gap, 0.0f), size);

    m_ctx.world.setMaterial(m_upper, m_ctx.engine.createMaterial(glm::vec3(1.0f, 0.45f, 0.1f), 0.9f));

    EntityHandle sun = m_ctx.world.create("sun", 0);
    m_ctx.world.setRotation(sun, glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(glm::radians(-20.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    m_ctx.world.add<LightComponent>(sun, LightComponent{LightComponent::Type::Directional, glm::vec3(1.0f, 0.95f, 0.85f), 1.1f, 0.0f, 1.0f});
    m_ctx.world.setVisible(sun, false);
    EntityHandle lamp = m_ctx.world.create("lamp", 0, glm::vec3(2.5f, 1.5f, 2.0f));
    m_ctx.world.add<LightComponent>(lamp, LightComponent{LightComponent::Type::Point, glm::vec3(0.2f, 0.6f, 1.0f), 3.0f, 12.0f, 1.0f});
    m_ctx.world.setVisible(lamp, false);

    m_ctx.world.camera().setPos(glm::vec3(1.5f, 0.6f, 5.0f));
}

void Scene::onUpdate(float deltaTime, float time) {
}

void Scene::rebind() {
    m_lower = m_ctx.world.find("lower");
    m_upper = m_ctx.world.find("upper");
}
