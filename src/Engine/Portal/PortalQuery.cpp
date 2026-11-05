module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

module Engine.Portal;

import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.Portal.view;

#include "PortalInternal.hpp"

namespace {
constexpr float kMinStepLength = 1.0e-9f;
constexpr float kUprightThreshold = 0.999f;
constexpr float kCrossingNudge = 1.0e-3f;
constexpr float kMinBlendReach = 1.0e-4f;
constexpr float kMinDirectionLength = 1.0e-12f;
constexpr int kGhostSkipAttempts = 8;
constexpr int kMaxRayCrossings = 256;
}

int PortalSystem::stepPoint(glm::vec3& position, int& cell, const glm::vec3& previous, bool upright) const {
    const glm::vec3 delta = position - previous;
    const float length = glm::length(delta);
    if (length < kMinStepLength) return -1;
    const glm::vec3 direction = delta / length;
    for (int id : portalsOf(cell)) {
        const Portal& portal = m_portals[static_cast<size_t>(id)];
        if (upright && transformDirection(portal.transform, glm::vec3(0.0f, 1.0f, 0.0f)).y < kUprightThreshold) continue;
        if (!crossing(portal, previous, direction, length)) continue;
        position = transformPoint(portal.transform, position);
        cell = m_portals[static_cast<size_t>(portal.partner)].cell;
        return id;
    }
    return -1;
}

std::optional<std::pair<int, float>> PortalSystem::firstCrossing(int cell, const glm::vec3& origin, const glm::vec3& direction, float maxT) const {
    std::optional<std::pair<int, float>> best;
    for (int id : portalsOf(cell)) {
        const float limit = best ? best->second : maxT;
        const auto distance = crossing(m_portals[static_cast<size_t>(id)], origin, direction, limit);
        if (distance && *distance < limit) best = std::make_pair(id, *distance);
    }
    return best;
}

int PortalSystem::step(Camera& camera) {
    if (m_portals.empty()) return -1;
    if (m_cameraCell < 0) m_cameraCell = std::max(cellAt(camera.getPosition()), 0);
    const glm::vec3 nearCenter = camera.getPosition() + cameraForward(camera) * camera.getNearPlane();
    if (!m_hasNear) {
        m_previousNear = nearCenter;
        m_hasNear = true;
        return -1;
    }
    glm::vec3 moved = nearCenter;
    int cell = m_cameraCell;
    const int crossedId = stepPoint(moved, cell, m_previousNear, true);
    if (crossedId < 0) {
        m_previousNear = nearCenter;
        m_cameraCell = resolveCell(m_cameraCell, nearCenter);
        return -1;
    }
    applyTransformToCamera(camera, m_portals[static_cast<size_t>(crossedId)].transform);
    m_cameraCell = cell;
    m_previousNear = camera.getPosition() + cameraForward(camera) * camera.getNearPlane();
    return crossedId;
}

PortalCellParams PortalSystem::sampleParams(int cell, const glm::vec3& position) const {
    if (cell < 0 || static_cast<size_t>(cell) >= m_cells.size()) return {};
    const PortalCellParams& base = m_cells[static_cast<size_t>(cell)].params;
    const float reach = std::max(m_settings.blendDistance, kMinBlendReach);
    struct Pull {
        float weight;
        const PortalCellParams* other;
        glm::vec3 gravity;
    };
    std::vector<Pull> pulls;
    float totalWeight = 0.0f;
    for (int id : portalsOf(cell)) {
        const Portal& portal = m_portals[static_cast<size_t>(id)];
        float side = 0.0f;
        const float distance = rectDistance(portal, position, side);
        const float weight = glm::clamp(0.5f * (1.0f - (side >= 0.0f ? distance : -distance) / reach), 0.0f, 1.0f);
        if (weight <= 0.0f) continue;
        const Portal& partner = m_portals[static_cast<size_t>(portal.partner)];
        const PortalCellParams& other = m_cells[static_cast<size_t>(partner.cell)].params;
        pulls.push_back({weight, &other, transformDirection(partner.transform, other.gravity)});
        totalWeight += weight;
    }
    const float scale = totalWeight > 1.0f ? 1.0f / totalWeight : 1.0f;
    PortalCellParams result = base;
    for (const Pull& pull : pulls) {
        const float weight = pull.weight * scale;
        result.gravity += weight * (pull.gravity - base.gravity);
        result.tint += weight * (pull.other->tint - base.tint);
        result.sun += weight * (pull.other->sun - base.sun);
        result.ambient += weight * (pull.other->ambient - base.ambient);
        for (const auto& [key, value] : pull.other->scalars) {
            const auto baseIt = base.scalars.find(key);
            const float from = baseIt != base.scalars.end() ? baseIt->second : 0.0f;
            const auto currentIt = result.scalars.find(key);
            result.scalars[key] = (currentIt != result.scalars.end() ? currentIt->second : from) + weight * (value - from);
        }
    }
    return result;
}

PortalRayHit PortalSystem::raycast(World& world, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, int cell, int maxCrossings) const {
    PortalRayHit result;
    result.cell = cell >= 0 ? cell : cellAt(origin);
    if (glm::dot(direction, direction) < kMinDirectionLength) {
        result.point = origin;
        return result;
    }
    glm::vec3 rayOrigin = origin;
    glm::vec3 rayDirection = glm::normalize(direction);
    float remaining = maxDistance;
    int current = result.cell;
    result.direction = rayDirection;
    float travelled = 0.0f;
    const int crossingLimit = std::min(maxCrossings, kMaxRayCrossings);
    for (int attempt = 0; attempt <= crossingLimit; ++attempt) {
        std::optional<RayHit> hit;
        float skipDistance = 0.0f;
        for (int tries = 0; tries < kGhostSkipAttempts; ++tries) {
            hit = world.raycast(rayOrigin + rayDirection * skipDistance, rayDirection, remaining - skipDistance);
            if (!hit) break;
            hit->distance += skipDistance;
            if (!isGhost(hit->entity)) break;
            skipDistance = hit->distance + kCrossingNudge;
            hit.reset();
        }
        float bestDistance = hit ? hit->distance : remaining;
        int bestPortal = -1;
        for (int id : portalsOf(current)) {
            const auto distance = crossing(m_portals[static_cast<size_t>(id)], rayOrigin, rayDirection, bestDistance);
            if (distance && *distance < bestDistance) {
                bestDistance = *distance;
                bestPortal = id;
            }
        }
        if (bestPortal < 0) {
            const float reached = hit ? hit->distance : remaining;
            result.hit = hit;
            result.distance = travelled + reached;
            if (result.hit) result.hit->distance = result.distance;
            result.point = rayOrigin + rayDirection * reached;
            result.cell = current;
            return result;
        }
        const Portal& portal = m_portals[static_cast<size_t>(bestPortal)];
        const glm::vec3 entryPoint = rayOrigin + rayDirection * bestDistance;
        rayOrigin = transformPoint(portal.transform, entryPoint);
        rayDirection = glm::normalize(transformDirection(portal.transform, rayDirection));
        rayOrigin = rayOrigin + rayDirection * kCrossingNudge;
        travelled += bestDistance + kCrossingNudge;
        remaining -= bestDistance + kCrossingNudge;
        current = m_portals[static_cast<size_t>(portal.partner)].cell;
        ++result.crossings;
        result.direction = rayDirection;
        if (remaining <= 0.0f) break;
    }
    result.distance = travelled;
    result.point = rayOrigin;
    result.cell = current;
    return result;
}

std::vector<PortalOverlap> PortalSystem::overlapSphere(World& world, const glm::vec3& center, float radius, int cell) const {
    thread_local std::vector<EntityHandle> found;
    thread_local std::vector<uint64_t> baseKeys;
    thread_local std::vector<uint64_t> laterKeys;
    std::vector<PortalOverlap> overlaps;
    baseKeys.clear();
    laterKeys.clear();
    world.overlapSphere(center, radius, found);
    overlaps.reserve(found.size());
    for (EntityHandle entity : found) {
        if (isGhost(entity)) continue;
        baseKeys.push_back(entityKey(entity));
        overlaps.push_back({entity, 0});
    }
    std::sort(baseKeys.begin(), baseKeys.end());
    baseKeys.erase(std::unique(baseKeys.begin(), baseKeys.end()), baseKeys.end());
    const int current = cell >= 0 ? cell : cellAt(center);
    for (int id : portalsOf(current)) {
        const Portal& portal = m_portals[static_cast<size_t>(id)];
        float side = 0.0f;
        if (rectDistance(portal, center, side) >= radius || side < -radius) continue;
        world.overlapSphere(transformPoint(portal.transform, center), radius, found);
        for (EntityHandle entity : found) {
            const uint64_t key = entityKey(entity);
            if (std::binary_search(baseKeys.begin(), baseKeys.end(), key) || std::find(laterKeys.begin(), laterKeys.end(), key) != laterKeys.end() || isGhost(entity)) continue;
            laterKeys.push_back(key);
            overlaps.push_back({entity, 1});
        }
    }
    return overlaps;
}
