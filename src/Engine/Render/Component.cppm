module;

#include <cstdint>
#include <cstddef>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

export module Engine.Render.component;

import Engine.Render.entity;
import Engine.glm;

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
        glm::vec3 currentScale = getScale();

        glm::vec3 axisX = (currentScale.x > 0.00001f) 
            ? (glm::vec3(localMatrix[0]) / currentScale.x) 
            : glm::vec3(1.0f, 0.0f, 0.0f);

        glm::vec3 axisY = (currentScale.y > 0.00001f) 
            ? (glm::vec3(localMatrix[1]) / currentScale.y) 
            : glm::vec3(0.0f, 1.0f, 0.0f);

        glm::vec3 axisZ = (currentScale.z > 0.00001f) 
            ? (glm::vec3(localMatrix[2]) / currentScale.z) 
            : glm::vec3(0.0f, 0.0f, 1.0f);

        localMatrix[0] = glm::vec4(axisX * newScale.x, 0.0f);
        localMatrix[1] = glm::vec4(axisY * newScale.y, 0.0f);
        localMatrix[2] = glm::vec4(axisZ * newScale.z, 0.0f);
    }

    [[nodiscard]] glm::quat getRot() const noexcept {
        glm::vec3 scale = getScale();

        glm::mat3 rotMatrix{
            (scale.x > 0.00001f) ? (glm::vec3(localMatrix[0]) / scale.x) : glm::vec3(1.0f, 0.0f, 0.0f),
            (scale.y > 0.00001f) ? (glm::vec3(localMatrix[1]) / scale.y) : glm::vec3(0.0f, 1.0f, 0.0f),
            (scale.z > 0.00001f) ? (glm::vec3(localMatrix[2]) / scale.z) : glm::vec3(0.0f, 0.0f, 1.0f)
        };

        return glm::quat_cast(rotMatrix);
    }

    void setRot(const glm::quat& newRotation) noexcept {
        glm::vec3 scale = getScale();
        glm::mat3 rotMatrix = glm::mat3_cast(glm::normalize(newRotation));

        localMatrix[0] = glm::vec4(rotMatrix[0] * scale.x, 0.0f);
        localMatrix[1] = glm::vec4(rotMatrix[1] * scale.y, 0.0f);
        localMatrix[2] = glm::vec4(rotMatrix[2] * scale.z, 0.0f);
    }
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