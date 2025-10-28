module;

#include <cstdint>

export module Engine.Render.component;

import Engine.Render.entity;
import Engine.glm;

export struct TransformComponent {
    glm::mat4 localMatrix{1.0f};
    glm::mat4 worldMatrix{1.0f};
};

export struct HierarchyComponent {
    Entity parent = INVALID_ENTITY;
};

export struct RenderableComponent {
    std::uint64_t mesh_uuid;
    std::uint64_t material_uuid;
    std::uint32_t shaderId;
    std::uint32_t objectId;
};
