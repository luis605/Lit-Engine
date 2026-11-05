module;

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

export module Engine.World:Builder;

import Engine.Render.entity;
import Engine.glm;
import :Types;
import :Core;

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
        m_steps.push_back([... captured = std::move(args)](World& world, EntityHandle entity) mutable { world.add<T>(entity, captured...); });
        return *this;
    }

    EntityHandle build() {
        const EntityHandle entity = m_world.create(m_desc);
        if (m_layer) m_world.setLayer(entity, *m_layer);
        if (!m_tag.empty()) m_world.setTag(entity, m_tag);
        if (!m_visible) m_world.setVisible(entity, false);
        for (auto& step : m_steps) step(m_world, entity);
        return entity;
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
