module;

#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.Profiler;
import Engine.camera;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr size_t kPackedVectorsPerLight = 4;

struct LightCandidate {
    float distance2;
    Entity index;
    glm::vec4 posRange;
    glm::vec4 colorIntensity;
    glm::vec4 dirCone;
    glm::vec4 params;
};

}

std::optional<glm::vec4> World::sceneBounds() {
    refreshSpatial();
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    bool any = false;
    for (const SpatialEntry& entry : m_spatial) {
        if (entry.state == 0) continue;
        any = true;
        low = glm::min(low, entry.center - glm::vec3(entry.radius));
        high = glm::max(high, entry.center + glm::vec3(entry.radius));
    }
    if (!any) return std::nullopt;
    const glm::vec3 center = (low + high) * 0.5f;
    return glm::vec4(center, glm::length(high - center));
}

Ray World::screenRay(float screenX, float screenY, float width, float height) const {
    if (!(width > 0.0f) || !(height > 0.0f)) return Ray{};
    const float ndcX = 2.0f * screenX / width - 1.0f;
    const float ndcY = 1.0f - 2.0f * screenY / height;
    const glm::mat4 inverse = glm::inverse(m_camera.getProjectionMatrix() * m_camera.getViewMatrix());
    const glm::vec4 nearPoint = inverse * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec4 farPoint = inverse * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec3 nearPosition = glm::vec3(nearPoint) / nearPoint.w;
    const glm::vec3 farPosition = glm::vec3(farPoint) / farPoint.w;
    return {nearPosition, glm::normalize(farPosition - nearPosition)};
}

glm::vec4 World::getWorldBounds(EntityHandle entity) const {
    if (!valid(entity) || !m_meshBounds || m_mesh[entity.index] == NO_MESH) return glm::vec4(0.0f);
    const glm::vec4 bounds = m_meshBounds(m_mesh[entity.index]);
    const glm::mat4 world = getWorldMatrix(entity);
    return glm::vec4(glm::vec3(world * glm::vec4(glm::vec3(bounds), 1.0f)), bounds.w * maxAxisScale(world));
}

bool collectLights(World& world, const glm::vec3& viewPos, LightSet& out, size_t maxLights) {
    std::optional<ProfileScope> profile;
    if (world.profiler()) profile.emplace(*world.profiler(), "collectLights");

    bool any = false;
    Entity directionalIndex = INVALID_ENTITY;
    out.directional = {glm::vec4(0.0f, 1.0f, 0.0f, 0.0f), glm::vec4(0.0f)};
    std::vector<LightCandidate> candidates;

    world.view<LightComponent>([&](EntityHandle entity, LightComponent& light) {
        any = true;
        if (light.type == LightComponent::Type::Directional) {
            if (directionalIndex != INVALID_ENTITY && entity.index > directionalIndex) return;
            directionalIndex = entity.index;
            out.directional[0] = glm::vec4(glm::normalize(glm::vec3(world.getWorldMatrix(entity)[2])), light.specular);
            out.directional[1] = glm::vec4(light.color * light.intensity, 1.0f);
            return;
        }
        const glm::vec3 position = world.getWorldPosition(entity);
        const glm::vec3 offset = position - viewPos;
        const bool spot = light.type == LightComponent::Type::Spot;
        const glm::vec3 direction = world.forward(entity);
        candidates.push_back({glm::dot(offset, offset), entity.index, glm::vec4(position, light.range), glm::vec4(light.color, light.intensity), glm::vec4(direction, std::cos(glm::radians(light.outerConeDegrees))), glm::vec4(std::cos(glm::radians(light.innerConeDegrees)), spot ? 1.0f : 0.0f, std::max(light.sourceRadius, 0.0f), std::max(light.sourceLength, 0.0f) * 0.5f)});
    });
    if (!any) return false;

    const auto closer = [](const LightCandidate& lhs, const LightCandidate& rhs) { return lhs.distance2 != rhs.distance2 ? lhs.distance2 < rhs.distance2 : lhs.index < rhs.index; };
    if (candidates.size() > maxLights) {
        std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(maxLights), candidates.end(), closer);
        candidates.resize(maxLights);
    } else {
        std::sort(candidates.begin(), candidates.end(), closer);
    }

    out.packed.clear();
    out.packed.reserve(candidates.size() * kPackedVectorsPerLight);
    for (const LightCandidate& candidate : candidates) {
        out.packed.push_back(candidate.posRange);
        out.packed.push_back(candidate.colorIntensity);
        out.packed.push_back(candidate.dirCone);
        out.packed.push_back(candidate.params);
    }
    out.count = static_cast<uint32_t>(candidates.size());
    return true;
}
