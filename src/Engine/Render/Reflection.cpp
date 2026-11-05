module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

module Engine.Render.reflection;

import Engine.glm;
import Engine.Render.settings;

namespace {

constexpr uint64_t kHashSeed = 1469598103934665603ull;
constexpr uint64_t kHashPrime = 1099511628211ull;
constexpr float kHashQuantum = 64.0f;
constexpr float kMinBoxSize = 0.01f;
constexpr float kBoundsPadding = 1.02f;
constexpr float kMinDirectionComponent = 1.0e-5f;
constexpr float kPlaneFacingEpsilon = 0.05f;
constexpr uint32_t kOccupiedAlpha = 0xFF000000u;
constexpr float kColorScale = 255.0f;

void mixHash(uint64_t& hash, float value) {
    hash = (hash ^ static_cast<uint64_t>(static_cast<int64_t>(std::lround(value * kHashQuantum)))) * kHashPrime;
}

void mixHash(uint64_t& hash, const glm::vec3& value) {
    mixHash(hash, value.x);
    mixHash(hash, value.y);
    mixHash(hash, value.z);
}

glm::ivec3 cellOf(const glm::vec3& gridPosition) {
    return glm::ivec3(static_cast<int>(std::floor(gridPosition.x)), static_cast<int>(std::floor(gridPosition.y)), static_cast<int>(std::floor(gridPosition.z)));
}

float volumeOf(const ReflectionBox& box) { return box.halfExtents.x * box.halfExtents.y * box.halfExtents.z; }

}

ReflectionTier reflectionTier(ReflectionQuality quality) {
    switch (quality) {
    case ReflectionQuality::Low: return {24, 24, 4, 0.75f};
    case ReflectionQuality::High: return {72, 64, 1, 1.0f};
    case ReflectionQuality::Medium: break;
    }
    return {40, 40, 2, 1.0f};
}

ReflectionPlan reflectionPlan(const RenderSettings& settings) {
    ReflectionPlan plan;
    plan.tier = reflectionTier(settings.reflectionQuality);
    switch (settings.reflectionMode) {
    case ReflectionMode::Off: return plan;
    case ReflectionMode::SSR: plan.features = kReflectionModeSsr; break;
    case ReflectionMode::SSRProbes: plan.features = kReflectionModeSsr | kReflectionModeProbes; break;
    case ReflectionMode::SSRPlanar: plan.features = kReflectionModeSsr | kReflectionModePlanar; break;
    case ReflectionMode::WorldSpace: plan.features = kReflectionModeSsr | kReflectionModeProbes | kReflectionModePlanar | kReflectionModeWorld; break;
    }
    return plan;
}

uint32_t ReflectionVoxelGrid::at(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 || x >= resolution || y >= resolution || z >= resolution) return 0u;
    return cells[(static_cast<std::size_t>(z) * resolution + static_cast<std::size_t>(y)) * resolution + static_cast<std::size_t>(x)];
}

uint32_t packVoxelColor(const glm::vec3& color) {
    const glm::vec3 clamped = glm::clamp(color, glm::vec3(0.0f), glm::vec3(1.0f)) * kColorScale + glm::vec3(0.5f);
    return static_cast<uint32_t>(clamped.x) | (static_cast<uint32_t>(clamped.y) << 8) | (static_cast<uint32_t>(clamped.z) << 16) | kOccupiedAlpha;
}

ReflectionVoxelGrid buildReflectionVoxels(const std::vector<ReflectionBox>& boxes, int resolution) {
    ReflectionVoxelGrid grid;
    if (boxes.empty() || resolution <= 0) return grid;
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const ReflectionBox& box : boxes) {
        low = glm::min(low, box.center - box.halfExtents);
        high = glm::max(high, box.center + box.halfExtents);
    }
    const glm::vec3 center = (low + high) * 0.5f;
    const glm::vec3 size = glm::max((high - low) * kBoundsPadding, glm::vec3(kMinBoxSize));
    grid.minCorner = center - size * 0.5f;
    grid.size = size;
    grid.resolution = resolution;
    grid.cells.assign(static_cast<std::size_t>(resolution) * resolution * resolution, 0u);

    std::vector<std::size_t> order(boxes.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t lhs, std::size_t rhs) { return volumeOf(boxes[lhs]) > volumeOf(boxes[rhs]); });

    const glm::vec3 cellsPerUnit = glm::vec3(static_cast<float>(resolution)) / size;
    const glm::ivec3 lastCell(resolution - 1);
    for (std::size_t index : order) {
        const ReflectionBox& box = boxes[index];
        const glm::ivec3 first = glm::clamp(cellOf((box.center - box.halfExtents - grid.minCorner) * cellsPerUnit), glm::ivec3(0), lastCell);
        const glm::ivec3 last = glm::clamp(cellOf((box.center + box.halfExtents - grid.minCorner) * cellsPerUnit), glm::ivec3(0), lastCell);
        const uint32_t packed = packVoxelColor(box.color);
        for (int z = first.z; z <= last.z; ++z) {
            for (int y = first.y; y <= last.y; ++y) {
                for (int x = first.x; x <= last.x; ++x) grid.cells[(static_cast<std::size_t>(z) * resolution + static_cast<std::size_t>(y)) * resolution + static_cast<std::size_t>(x)] = packed;
            }
        }
    }
    return grid;
}

glm::vec3 boxProjectDirection(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& center, const glm::vec3& halfExtents) {
    glm::vec3 safe = direction;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(safe[axis]) < kMinDirectionComponent) safe[axis] = kMinDirectionComponent;
    }
    const glm::vec3 first = (center + halfExtents - position) / safe;
    const glm::vec3 second = (center - halfExtents - position) / safe;
    const glm::vec3 farthest = glm::max(first, second);
    const float wallDistance = std::max(std::min(farthest.x, std::min(farthest.y, farthest.z)), 0.0f);
    const glm::vec3 hit = position + direction * wallDistance - center;
    const float length = glm::length(hit);
    return length > kMinDirectionComponent ? hit / length : direction;
}

float probeInfluence(const glm::vec3& position, const ReflectionProbe& probe) {
    const glm::vec3 margin = probe.halfExtents - glm::abs(position - probe.center);
    const float nearestWall = std::min(margin.x, std::min(margin.y, margin.z));
    return std::clamp(nearestWall / std::max(probe.blendDistance, 1.0e-3f), 0.0f, 1.0f);
}

int strongestProbe(const std::vector<ReflectionProbe>& probes, const glm::vec3& position) {
    int best = -1;
    float bestWeight = 0.0f;
    const int count = static_cast<int>(std::min<std::size_t>(probes.size(), kMaxReflectionProbes));
    for (int index = 0; index < count; ++index) {
        const float weight = probeInfluence(position, probes[static_cast<std::size_t>(index)]);
        if (weight > bestWeight) {
            bestWeight = weight;
            best = index;
        }
    }
    return best;
}

int selectPlanarPlane(const std::vector<ReflectionPlane>& planes, const glm::vec3& cameraPosition) {
    int best = -1;
    float bestDistance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < planes.size(); ++index) {
        const ReflectionPlane& plane = planes[index];
        const glm::vec3 offset = cameraPosition - plane.center;
        const float height = glm::dot(offset, plane.normal);
        if (height <= kPlaneFacingEpsilon) continue;
        const float distance = glm::dot(offset, offset);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(index);
        }
    }
    return best;
}

uint32_t clampProbeResolution(uint32_t resolution) {
    uint32_t snapped = kProbeResolutionMin;
    while (snapped < resolution && snapped < kProbeResolutionMax) snapped <<= 1;
    return snapped;
}

uint32_t sceneProbeResolution(const std::vector<ReflectionProbe>& probes) {
    uint32_t resolution = kProbeResolutionMin;
    for (const ReflectionProbe& probe : probes) resolution = std::max(resolution, clampProbeResolution(probe.resolution));
    return resolution;
}

bool probeBakeNeeded(const ProbeBakeState& state, int probeCount, uint32_t resolution) {
    if (probeCount <= 0) return false;
    return state.requested || state.bakedCount != probeCount || state.bakedResolution != resolution;
}

uint64_t hashReflectionBoxes(const std::vector<ReflectionBox>& boxes) {
    uint64_t hash = kHashSeed;
    for (const ReflectionBox& box : boxes) {
        mixHash(hash, box.center);
        mixHash(hash, box.halfExtents);
        mixHash(hash, box.color);
    }
    return hash;
}
