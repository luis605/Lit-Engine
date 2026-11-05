module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.reflection;
import Engine.glm;

namespace {

constexpr float kMinBoxExtent = 0.05f;
constexpr float kMinBoxVolume = 0.02f;
constexpr float kOpaqueAlpha = 0.99f;
constexpr float kMinAxisLength = 1.0e-4f;

glm::vec3 normalizedAxis(const glm::vec4& column) {
    const glm::vec3 axis(column);
    const float length = glm::length(axis);
    return length > kMinAxisLength ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

ReflectionBox orientedBounds(const glm::mat4& world, const glm::vec3& localCenter, const glm::vec3& localHalf) {
    const glm::vec3 center = glm::vec3(world * glm::vec4(localCenter, 1.0f));
    glm::vec3 half(0.0f);
    for (int column = 0; column < 3; ++column) {
        const glm::vec3 axis = glm::vec3(world[column]) * localHalf[column];
        half += glm::abs(axis);
    }
    ReflectionBox box;
    box.center = center;
    box.halfExtents = half;
    return box;
}

}

void collectReflectionVolumes(World& world, ReflectionScene& out) {
    out.probes.clear();
    out.planes.clear();
    world.view<ReflectionProbeComponent>([&](EntityHandle entity, ReflectionProbeComponent& probe) {
        if (out.probes.size() >= static_cast<size_t>(kMaxReflectionProbes) || !world.isVisible(entity)) return;
        ReflectionProbe volume;
        volume.center = world.getWorldPosition(entity);
        volume.halfExtents = probe.halfExtents;
        volume.intensity = probe.intensity;
        volume.blendDistance = probe.blendDistance;
        volume.resolution = clampProbeResolution(probe.resolution);
        out.probes.push_back(volume);
    });
    world.view<PlanarReflectionComponent>([&](EntityHandle entity, PlanarReflectionComponent& plane) {
        if (!world.isVisible(entity)) return;
        const glm::mat4 matrix = world.getWorldMatrix(entity);
        ReflectionPlane volume;
        volume.center = glm::vec3(matrix[3]);
        volume.normal = normalizedAxis(matrix[1]);
        volume.axisU = normalizedAxis(matrix[0]);
        volume.halfSize = plane.halfSize;
        out.planes.push_back(volume);
    });
}

void collectReflectionBoxes(World& world, const ReflectionBoxSource& source, ReflectionScene& out) {
    out.boxes.clear();
    if (!source.meshBox || !source.materialColor) return;
    world.forEach([&](EntityHandle entity) {
        if (!world.isVisible(entity)) return;
        const uint32_t mesh = world.getMesh(entity);
        if (mesh == NO_MESH) return;
        const RenderableComponent& renderable = world.getRenderable(entity);
        if (renderable.alpha < kOpaqueAlpha || (renderable.flags & RENDER_HIDDEN) != 0u) return;
        glm::vec3 localCenter(0.0f);
        glm::vec3 localHalf(0.0f);
        if (!source.meshBox(mesh, localCenter, localHalf)) return;
        ReflectionBox box = orientedBounds(world.getWorldMatrix(entity), localCenter, localHalf);
        box.halfExtents = glm::max(box.halfExtents, glm::vec3(kMinBoxExtent));
        if (box.halfExtents.x * box.halfExtents.y * box.halfExtents.z < kMinBoxVolume) return;
        box.color = source.materialColor(renderable.material_uuid);
        out.boxes.push_back(box);
    });
    if (out.boxes.size() <= source.maxBoxes) return;
    std::partial_sort(out.boxes.begin(), out.boxes.begin() + static_cast<std::ptrdiff_t>(source.maxBoxes), out.boxes.end(), [](const ReflectionBox& lhs, const ReflectionBox& rhs) {
        return lhs.halfExtents.x * lhs.halfExtents.y * lhs.halfExtents.z > rhs.halfExtents.x * rhs.halfExtents.y * rhs.halfExtents.z;
    });
    out.boxes.resize(source.maxBoxes);
}
