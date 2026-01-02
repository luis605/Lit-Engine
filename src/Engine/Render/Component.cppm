module;

#include <cstdint>
#include <cstddef>

export module Engine.Render.component;

import Engine.Render.entity;
import Engine.glm;

export struct TransformComponent {
    glm::mat4 localMatrix{1.0f};
    glm::mat4 worldMatrix{1.0f};
};

export struct HierarchyComponent {
    Entity parent = INVALID_ENTITY;
    uint32_t level = 0;
};

export struct RenderableComponent {
    std::uint32_t mesh_uuid;
    std::uint32_t material_uuid;
    std::uint32_t shaderId;
    std::uint32_t objectId;
    alignas(4) float alpha = 1.0f;
};