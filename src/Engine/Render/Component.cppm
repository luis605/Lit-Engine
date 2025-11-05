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

static_assert(offsetof(TransformComponent, localMatrix) == 0, "Offset mismatch for localMatrix");
static_assert(offsetof(TransformComponent, worldMatrix) == 64, "Offset mismatch for worldMatrix");
static_assert(sizeof(TransformComponent) == 128, "Size mismatch for TransformComponent");

export struct HierarchyComponent {
    Entity parent = INVALID_ENTITY;
    uint32_t level = 0;
};

static_assert(offsetof(HierarchyComponent, parent) == 0, "Offset mismatch for parent");
static_assert(offsetof(HierarchyComponent, level) == 4, "Offset mismatch for level");
static_assert(sizeof(HierarchyComponent) == 8, "Size mismatch for HierarchyComponent");

export struct RenderableComponent {
    std::uint32_t mesh_uuid;
    std::uint32_t material_uuid;
    std::uint32_t shaderId;
    std::uint32_t objectId;
    alignas(4) float alpha = 1.0f;
};

static_assert(offsetof(RenderableComponent, mesh_uuid) == 0, "Offset mismatch for mesh_uuid");
static_assert(offsetof(RenderableComponent, material_uuid) == 4, "Offset mismatch for material_uuid");
static_assert(offsetof(RenderableComponent, shaderId) == 8, "Offset mismatch for shaderId");
static_assert(offsetof(RenderableComponent, objectId) == 12, "Offset mismatch for objectId");
static_assert(offsetof(RenderableComponent, alpha) == 16, "Offset mismatch for alpha");
static_assert(sizeof(RenderableComponent) == 20, "Size mismatch for RenderableComponent");
