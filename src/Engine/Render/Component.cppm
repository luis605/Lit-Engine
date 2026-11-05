module;

#include <cstdint>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Engine.Render.component;

import Engine.Render.entity;
import Engine.glm;

namespace component_detail {
inline constexpr float kMinAxisLength = 0.00001f;

inline glm::vec3 normalizedAxis(const glm::vec4& column, float length, const glm::vec3& fallback) noexcept {
    return length > kMinAxisLength ? glm::vec3(column) / length : fallback;
}
}

export struct TransformComponent {
    glm::mat4 localMatrix{1.0f};

    [[nodiscard]] glm::vec3 getPos() const noexcept {
        return glm::vec3(localMatrix[3]);
    }

    void setPos(const glm::vec3& location) noexcept {
        localMatrix[3] = glm::vec4(location, 1.0f);
    }

    [[nodiscard]] glm::vec3 getScale() const noexcept {
        return glm::vec3{
            glm::length(glm::vec3(localMatrix[0])),
            glm::length(glm::vec3(localMatrix[1])),
            glm::length(glm::vec3(localMatrix[2]))
        };
    }

    void setScale(const glm::vec3& newScale) noexcept {
        const glm::mat3 axes = rotationAxes(getScale());
        localMatrix[0] = glm::vec4(axes[0] * newScale.x, 0.0f);
        localMatrix[1] = glm::vec4(axes[1] * newScale.y, 0.0f);
        localMatrix[2] = glm::vec4(axes[2] * newScale.z, 0.0f);
    }

    [[nodiscard]] glm::quat getRot() const noexcept {
        return glm::quat_cast(rotationAxes(getScale()));
    }

    void setRot(const glm::quat& newRotation) noexcept {
        const glm::vec3 scale = getScale();
        const glm::mat3 rotationMatrix = glm::mat3_cast(glm::normalize(newRotation));

        localMatrix[0] = glm::vec4(rotationMatrix[0] * scale.x, 0.0f);
        localMatrix[1] = glm::vec4(rotationMatrix[1] * scale.y, 0.0f);
        localMatrix[2] = glm::vec4(rotationMatrix[2] * scale.z, 0.0f);
    }

  private:
    [[nodiscard]] glm::mat3 rotationAxes(const glm::vec3& scale) const noexcept {
        return glm::mat3{
            component_detail::normalizedAxis(localMatrix[0], scale.x, glm::vec3(1.0f, 0.0f, 0.0f)),
            component_detail::normalizedAxis(localMatrix[1], scale.y, glm::vec3(0.0f, 1.0f, 0.0f)),
            component_detail::normalizedAxis(localMatrix[2], scale.z, glm::vec3(0.0f, 0.0f, 1.0f))
        };
    }
};

export struct HierarchyComponent {
    Entity parent = INVALID_ENTITY;
    uint32_t level = 0;
};

export inline constexpr std::uint32_t NO_MESH = 0xFFFFFFFFu;
export inline constexpr std::uint32_t RENDER_HIDDEN = 1u;
export inline constexpr std::uint32_t RENDER_NO_OCCLUDER = 2u;
export inline constexpr std::uint32_t RENDER_MASKED = 8u;

export struct RenderableComponent {
    std::uint32_t mesh_uuid = 0;
    std::uint32_t material_uuid = 0;
    std::uint32_t shaderId = 0;
    std::uint32_t objectId = 0;
    alignas(4) float alpha = 1.0f;
    std::uint32_t flags = 0;
};