module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

module Engine.Nav;

namespace {
constexpr float kStaleSampleTileFraction = 0.25f;
constexpr int kMaxSampleSteps = 65536;
}

bool NavMesh::pathStale(const NavPath& path, uint64_t sinceVersion) const {
    if (!path.found) return true;
    if (m_resetVersion > sinceVersion || m_linkChangeVersion > sinceVersion) return true;
    const float sampleStep = std::max(m_config.cellSize, m_config.cellSize * static_cast<float>(m_config.tileCells) * kStaleSampleTileFraction);
    for (size_t index = 0; index < path.points.size(); ++index) {
        const glm::vec3 start = path.points[index].position;
        const glm::vec3 end = index + 1 < path.points.size() ? path.points[index + 1].position : start;
        const glm::vec3 delta = end - start;
        const float distance = std::sqrt(delta.x * delta.x + delta.z * delta.z);
        const float stepCount = std::ceil(distance / sampleStep);
        if (!(stepCount <= static_cast<float>(kMaxSampleSteps))) return true;
        const int steps = std::max(1, static_cast<int>(stepCount));
        for (int step = 0; step <= steps; ++step) {
            const glm::vec3 sample = start + delta * (static_cast<float>(step) / static_cast<float>(steps));
            const Tile* tile = tileAt(cellOf(sample.x, m_config.originX), cellOf(sample.z, m_config.originZ));
            if (!tile || tile->builtVersion > sinceVersion) return true;
        }
    }
    return false;
}

std::optional<float> NavMesh::groundHeight(const glm::vec3& position) const {
    const float* heights = nullptr;
    const uint32_t count = layersAt(cellOf(position.x, m_config.originX), cellOf(position.z, m_config.originZ), heights);
    int best = -1;
    float bestDifference = m_config.maxClimb;
    for (uint32_t layer = 0; layer < count; ++layer) {
        const float difference = std::fabs(heights[layer] - position.y);
        if (difference <= bestDifference) {
            bestDifference = difference;
            best = static_cast<int>(layer);
        }
    }
    if (best < 0) return std::nullopt;
    return heights[best];
}
