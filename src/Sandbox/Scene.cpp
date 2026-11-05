module;

#include <string>
#include <typeinfo>

module Sandbox.scene;

import Engine.Render.component;
import Engine.glm;

void Scene::onStart() {
    const glm::vec3 size(0.5f, 2.5f, 0.5f);
    const float gap = 0.2f;
    constexpr int EXTRA_LAMP_COUNT = 4;
    constexpr float EXTRA_LAMP_START_X = -2.0f;
    constexpr float EXTRA_LAMP_SPACING = 1.6f;

    m_lower = m_ctx.world.create("lower", m_ctx.cubeMesh, glm::vec3(0.0f), size);
    m_upper = m_ctx.world.create("upper", m_ctx.cubeMesh, glm::vec3(0.0f, size.y + gap, 0.0f), size);

    m_hand = m_ctx.world.create("hand", m_ctx.sphereMesh, glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(1.2f, 0.4f, 1.2f), m_upper);
    m_ctx.world.setMaterial(m_upper, m_ctx.engine.createMaterial(glm::vec3(1.0f, 0.45f, 0.1f), 0.9f));

    const EntityHandle sun = m_ctx.world.create("sun");
    m_ctx.world.setRotation(sun, glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)) * glm::angleAxis(glm::radians(-20.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    m_ctx.world.add<LightComponent>(sun, LightComponent{LightComponent::Type::Directional, glm::vec3(1.0f, 0.95f, 0.85f), 0.5f, 0.0f, 1.0f});
    const EntityHandle lamp = m_ctx.world.create("lamp", NO_MESH, glm::vec3(2.5f, 1.5f, 2.0f));
    m_ctx.world.add<LightComponent>(lamp, LightComponent{LightComponent::Type::Point, glm::vec3(0.2f, 0.6f, 1.0f), 3.0f, 12.0f, 1.0f});

    const glm::vec3 lampColors[EXTRA_LAMP_COUNT] = {glm::vec3(1.0f, 0.2f, 0.2f), glm::vec3(0.2f, 1.0f, 0.3f), glm::vec3(1.0f, 0.9f, 0.2f), glm::vec3(0.8f, 0.2f, 1.0f)};
    for (int lampIndex = 0; lampIndex < EXTRA_LAMP_COUNT; ++lampIndex) {
        const EntityHandle extra = m_ctx.world.create("lamp" + std::to_string(lampIndex), NO_MESH, glm::vec3(EXTRA_LAMP_START_X + static_cast<float>(lampIndex) * EXTRA_LAMP_SPACING, -0.5f, 2.2f));
        m_ctx.world.add<LightComponent>(extra, LightComponent{LightComponent::Type::Point, lampColors[lampIndex], 1.2f, 5.0f, 1.0f});
    }
    const EntityHandle spot = m_ctx.world.create("spot", NO_MESH, glm::vec3(0.5f, 4.0f, 3.0f));
    m_ctx.world.lookAt(spot, glm::vec3(1.0f, 0.5f, 0.0f));
    m_ctx.world.add<LightComponent>(spot, LightComponent{LightComponent::Type::Spot, glm::vec3(1.0f, 1.0f, 1.0f), 2.5f, 12.0f, 1.0f, 8.0f, 20.0f});

    m_ctx.world.camera().setPos(glm::vec3(1.5f, 0.6f, 5.0f));
}

void Scene::onUpdate(float, float) {}

void Scene::rebind() {
    m_lower = m_ctx.world.find("lower");
    m_upper = m_ctx.world.find("upper");
    m_hand = m_ctx.world.find("hand");
}
