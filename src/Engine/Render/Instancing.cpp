module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

module Engine.Instancing;

namespace {

constexpr uint32_t kSeedMask = 0x00FFFFFFu;
constexpr float kLodDiameterPx[kInstanceLodLevels - 1] = {60.0f, 40.0f, 25.0f, 10.0f, 4.0f, 4.0f};  // mesh.glsl LOD_DIAMETER_PX

uint32_t packSnorm16x2(float a, float b) {
    auto one = [](float v) { return static_cast<uint32_t>(static_cast<uint16_t>(static_cast<int16_t>(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)))); };
    return one(a) | (one(b) << 16u);
}

glm::vec2 unpackSnorm16x2(uint32_t v) {
    auto one = [](uint32_t bits) { return std::max(static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(bits))) / 32767.0f, -1.0f); };
    return glm::vec2(one(v & 0xFFFFu), one(v >> 16u));
}

uint32_t expandBits(uint32_t v) {
    v &= 0x3FFu;
    v = (v | (v << 16u)) & 0x030000FFu;
    v = (v | (v << 8u)) & 0x0300F00Fu;
    v = (v | (v << 4u)) & 0x030C30C3u;
    v = (v | (v << 2u)) & 0x09249249u;
    return v;
}

glm::vec4 instanceSphere(const InstanceData& instance, const glm::vec4& meshSphere) {
    const glm::vec3 center = instance.position + instance.rotation * (glm::vec3(meshSphere) * instance.scale);
    return glm::vec4(center, meshSphere.w * std::abs(instance.scale));
}

float maxAxisScale(const glm::mat4& m) { return std::max({glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2]))}); }

bool sphereInside(const std::array<glm::vec4, 6>& planes, const glm::vec3& center, float radius) {
    for (const glm::vec4& plane : planes) {
        if (glm::dot(glm::vec3(plane), center) + plane.w < -radius) return false;
    }
    return true;
}

}

void InstanceBatch::assign(std::span<const InstanceData> data) {
    instances.assign(data.begin(), data.end());
    if (!keys.empty() && keys.size() != instances.size()) keys.clear();
    markLayout();
}

void InstanceBatch::update(uint32_t first, std::span<const InstanceData> data) {
    if (data.empty()) return;
    const size_t end = static_cast<size_t>(first) + data.size();
    if (end > instances.size()) {
        instances.resize(end);
        if (!keys.empty()) keys.resize(end, 0);
        markLayout();
    }
    std::copy(data.begin(), data.end(), instances.begin() + first);
    markDirty(first, static_cast<uint32_t>(end));
}

bool InstanceBatch::remove(uint32_t index) {
    if (index >= instances.size() || (instances[index].flags & InstanceFlags::Dead) != 0) return false;
    instances[index].flags |= InstanceFlags::Dead;
    markDirty(index, index + 1);
    return true;
}

bool InstanceBatch::revive(uint32_t index) {
    if (index >= instances.size() || (instances[index].flags & InstanceFlags::Dead) == 0) return false;
    instances[index].flags &= ~InstanceFlags::Dead;
    markDirty(index, index + 1);
    return true;
}

std::optional<uint64_t> InstanceBatch::swapRemove(uint32_t index) {
    if (index >= instances.size()) return std::nullopt;
    const size_t last = instances.size() - 1;
    std::optional<uint64_t> moved;
    if (index != last) {
        instances[index] = instances[last];
        if (!keys.empty()) {
            keys[index] = keys[last];
            moved = keys[index];
        }
    }
    instances.pop_back();
    if (!keys.empty()) keys.pop_back();
    markLayout();
    return moved;
}

std::optional<uint32_t> InstanceBatch::indexOfKey(uint64_t key) const {
    const auto it = std::find(keys.begin(), keys.end(), key);
    if (it == keys.end()) return std::nullopt;
    return static_cast<uint32_t>(it - keys.begin());
}

uint32_t InstanceBatch::aliveCount() const {
    return static_cast<uint32_t>(std::count_if(instances.begin(), instances.end(), [](const InstanceData& i) { return (i.flags & InstanceFlags::Dead) == 0; }));
}

void InstanceBatch::setSettings(const InstanceBatchSettings& value) {
    if (value == settings) return;
    settings = value;
    markLayout();
}

void InstanceBatch::markDirty(uint32_t first, uint32_t end) {
    dirtyFirst = std::min(dirtyFirst, first);
    dirtyEnd = std::max(dirtyEnd, end);
    ++version;
}

void InstanceBatch::markLayout() {
    ++layoutVersion;
    ++version;
    clearDirty();
}

InstanceGpu packInstance(const InstanceData& instance) {
    InstanceGpu out;
    out.positionScale = glm::vec4(instance.position, instance.scale);
    glm::quat q = glm::normalize(instance.rotation);
    if (q.w < 0.0f) q = -q;
    out.packed[0] = packSnorm16x2(q.x, q.y);
    out.packed[1] = packSnorm16x2(q.z, q.w);
    out.packed[2] = instance.color;
    out.packed[3] = (instance.seed & kSeedMask) | ((instance.flags & 0xFFu) << 24u);
    return out;
}

InstanceData unpackInstance(const InstanceGpu& gpu) {
    InstanceData out;
    out.position = glm::vec3(gpu.positionScale);
    out.scale = gpu.positionScale.w;
    const glm::vec2 xy = unpackSnorm16x2(gpu.packed[0]);
    const glm::vec2 zw = unpackSnorm16x2(gpu.packed[1]);
    out.rotation = glm::normalize(glm::quat(zw.y, xy.x, xy.y, zw.x));
    out.color = gpu.packed[2];
    out.seed = gpu.packed[3] & kSeedMask;
    out.flags = gpu.packed[3] >> 24u;
    return out;
}

float instanceHash01(uint32_t seed) {
    uint32_t h = seed * 747796405u + 2891336453u;
    h = ((h >> ((h >> 28u) + 4u)) ^ h) * 277803737u;
    h = (h >> 22u) ^ h;
    return static_cast<float>(h & kSeedMask) * (1.0f / 16777216.0f);
}

glm::vec4 instanceColorFloat(uint32_t rgba8) {
    return glm::vec4(static_cast<float>(rgba8 & 0xFFu), static_cast<float>((rgba8 >> 8u) & 0xFFu), static_cast<float>((rgba8 >> 16u) & 0xFFu), static_cast<float>(rgba8 >> 24u)) * (1.0f / 255.0f);
}

uint32_t packInstanceColor(const glm::vec4& color) {
    auto byte = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return byte(color.r) | (byte(color.g) << 8u) | (byte(color.b) << 16u) | (byte(color.a) << 24u);
}

std::vector<uint32_t> instanceSpatialOrder(std::span<const InstanceData> instances) {
    std::vector<uint32_t> order(instances.size());
    std::iota(order.begin(), order.end(), 0u);
    if (instances.size() <= kInstanceCellSize) return order;
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const InstanceData& instance : instances) {
        low = glm::min(low, instance.position);
        high = glm::max(high, instance.position);
    }
    const glm::vec3 extent = glm::max(high - low, glm::vec3(1.0e-6f));
    // Uniform quantisation over the largest axis keeps cells roughly cubic on flat (xz) distributions.
    const float span = std::max({extent.x, extent.y, extent.z});
    std::vector<uint32_t> codes(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        const glm::vec3 q = glm::clamp((instances[i].position - low) / span, glm::vec3(0.0f), glm::vec3(1.0f)) * 1023.0f;
        codes[i] = expandBits(static_cast<uint32_t>(q.x)) | (expandBits(static_cast<uint32_t>(q.y)) << 1u) | (expandBits(static_cast<uint32_t>(q.z)) << 2u);
    }
    std::stable_sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return codes[a] < codes[b]; });
    return order;
}

InstanceCellGpu buildInstanceCell(std::span<const InstanceData> instances, std::span<const uint32_t> slots, const glm::vec4& meshSphere, uint32_t batch) {
    InstanceCellGpu cell;
    cell.info[0] = batch;
    cell.info[1] = static_cast<uint32_t>(slots.size());
    glm::vec3 center(0.0f);
    uint32_t alive = 0;
    for (const uint32_t slot : slots) {
        const InstanceData& instance = instances[slot];
        if ((instance.flags & InstanceFlags::Dead) != 0) continue;
        center += glm::vec3(instanceSphere(instance, meshSphere));
        ++alive;
    }
    cell.info[2] = alive;
    if (alive == 0) {
        cell.sphere = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
        return cell;
    }
    center /= static_cast<float>(alive);
    float radius = 0.0f;
    for (const uint32_t slot : slots) {
        const InstanceData& instance = instances[slot];
        if ((instance.flags & InstanceFlags::Dead) != 0) continue;
        const glm::vec4 sphere = instanceSphere(instance, meshSphere);
        radius = std::max(radius, glm::length(glm::vec3(sphere) - center) + sphere.w);
    }
    cell.sphere = glm::vec4(center, radius * 1.0001f + 1.0e-5f);
    return cell;
}

std::optional<uint32_t> InstanceAllocator::allocate(uint32_t cells) {
    if (cells == 0) return std::nullopt;
    auto best = m_free.end();
    for (auto it = m_free.begin(); it != m_free.end(); ++it) {
        if (it->second >= cells && (best == m_free.end() || it->second < best->second)) best = it;
    }
    if (best != m_free.end()) {
        const uint32_t first = best->first;
        const uint32_t remaining = best->second - cells;
        m_free.erase(best);
        if (remaining > 0) m_free.emplace(first + cells, remaining);
        m_used += cells;
        return first;
    }
    if (static_cast<uint64_t>(m_highWater) + cells > m_limit) return std::nullopt;
    const uint32_t first = m_highWater;
    m_highWater += cells;
    m_used += cells;
    return first;
}

void InstanceAllocator::free(uint32_t first, uint32_t cells) {
    if (cells == 0) return;
    m_used -= std::min(m_used, cells);
    auto [it, inserted] = m_free.emplace(first, cells);
    if (!inserted) return;
    // Merge with the following and the preceding range.
    auto next = std::next(it);
    if (next != m_free.end() && it->first + it->second == next->first) {
        it->second += next->second;
        m_free.erase(next);
    }
    if (it != m_free.begin()) {
        auto previous = std::prev(it);
        if (previous->first + previous->second == it->first) {
            previous->second += it->second;
            m_free.erase(it);
            it = previous;
        }
    }
    if (it->first + it->second == m_highWater) {
        m_highWater = it->first;
        m_free.erase(it);
    }
}

void InstanceAllocator::clear() {
    m_free.clear();
    m_highWater = 0;
    m_used = 0;
}

uint32_t instanceLodForDiameter(float diameterPx) {
    for (uint32_t lod = 0; lod < kInstanceLodLevels - 1; ++lod) {
        if (diameterPx >= kLodDiameterPx[lod]) return lod;
    }
    return kInstanceLodLevels - 1;
}

std::array<glm::vec4, 6> instanceFrustumPlanes(const glm::mat4& m) {
    // Rows of the (column-major) view-projection; depth range [0, 1] (Vulkan), reversed or not.
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    std::array<glm::vec4, 6> planes = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (glm::vec4& plane : planes) plane /= std::max(glm::length(glm::vec3(plane)), 1.0e-12f);
    return planes;
}

InstanceCullResult cullInstancesReference(const InstanceBatch& batch, const glm::mat4& owner, const glm::vec4& meshSphere, const InstanceCullView& view) {
    InstanceCullResult result;
    if (batch.instances.empty()) return result;
    const std::vector<uint32_t> order = instanceSpatialOrder(batch.instances);
    const float ownerScale = maxAxisScale(owner);
    const float drawDistance = batch.settings.drawDistance * view.distanceScale;
    const float fadeStart = batch.settings.fadeStart * view.distanceScale;
    for (size_t first = 0; first < order.size(); first += kInstanceCellSize) {
        const size_t count = std::min<size_t>(kInstanceCellSize, order.size() - first);
        const std::span<const uint32_t> slots(order.data() + first, count);
        const InstanceCellGpu cell = buildInstanceCell(batch.instances, slots, meshSphere, 0);
        ++result.cellsTested;
        if (cell.info[2] == 0) continue;
        if (view.cellCull) {
            const glm::vec3 center = glm::vec3(owner * glm::vec4(glm::vec3(cell.sphere), 1.0f));
            const float radius = cell.sphere.w * ownerScale;
            if (!sphereInside(view.planes, center, radius)) continue;
            if (drawDistance > 0.0f && glm::length(center - view.position) - radius > drawDistance) continue;
        }
        ++result.cellsAccepted;
        for (const uint32_t slot : slots) {
            const InstanceData& instance = batch.instances[slot];
            if ((instance.flags & InstanceFlags::Dead) != 0) continue;
            ++result.instancesTested;
            const glm::vec4 local = instanceSphere(instance, meshSphere);
            const glm::vec3 center = glm::vec3(owner * glm::vec4(glm::vec3(local), 1.0f));
            const float radius = local.w * ownerScale;
            if (!sphereInside(view.planes, center, radius)) continue;
            const float distance = glm::length(center - view.position);
            if (drawDistance > 0.0f) {
                if (distance > drawDistance) continue;
                if (fadeStart > 0.0f && fadeStart < drawDistance && distance > fadeStart) {
                    const float t = (distance - fadeStart) / (drawDistance - fadeStart);
                    if (instanceHash01(instance.seed) < t) continue;
                }
            }
            const float depth = std::max(-(view.view * glm::vec4(center, 1.0f)).z, 0.001f);
            uint32_t lod = 0;
            if (view.forcedLod >= 0) lod = std::min<uint32_t>(static_cast<uint32_t>(view.forcedLod), kInstanceLodLevels - 1);
            else if (depth > radius) lod = instanceLodForDiameter(view.lodBias * batch.settings.lodBias * radius * view.pixelScale / std::max(depth, 1.0e-4f));
            ++result.visible;
            ++result.perLod[lod];
        }
    }
    return result;
}

std::string instanceBatchText(const InstanceBatch& batch) {
    std::ostringstream out;
    out.precision(9);
    out << batch.mesh << ' ' << batch.material << ' ' << batch.shader << ' ' << batch.settings.drawDistance << ' ' << batch.settings.fadeStart << ' ' << batch.settings.lodBias << ' ' << (batch.settings.castShadows ? 1 : 0) << ' ' << batch.instances.size() << ' ' << (batch.keys.empty() ? 0 : 1);
    for (size_t i = 0; i < batch.instances.size(); ++i) {
        const InstanceData& d = batch.instances[i];
        const glm::quat q = glm::normalize(d.rotation);
        out << ' ' << d.position.x << ' ' << d.position.y << ' ' << d.position.z << ' ' << d.scale << ' ' << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' ' << d.color << ' ' << d.seed << ' ' << d.flags;
        if (!batch.keys.empty()) out << ' ' << batch.keys[i];
    }
    return out.str();
}

std::optional<InstanceBatch> instanceBatchFromText(const std::string& text) {
    std::istringstream in(text);
    InstanceBatch batch;
    int shadows = 1;
    size_t count = 0;
    int withKeys = 0;
    if (!(in >> batch.mesh >> batch.material >> batch.shader >> batch.settings.drawDistance >> batch.settings.fadeStart >> batch.settings.lodBias >> shadows >> count >> withKeys)) return std::nullopt;
    if (count > kMaxPoolInstances) return std::nullopt;
    batch.settings.castShadows = shadows != 0;
    batch.instances.resize(count);
    if (withKeys != 0) batch.keys.resize(count);
    for (size_t i = 0; i < count; ++i) {
        InstanceData& d = batch.instances[i];
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!(in >> d.position.x >> d.position.y >> d.position.z >> d.scale >> w >> x >> y >> z >> d.color >> d.seed >> d.flags)) return std::nullopt;
        d.rotation = glm::quat(w, x, y, z);
        if (withKeys != 0 && !(in >> batch.keys[i])) return std::nullopt;
    }
    batch.markLayout();
    return batch;
}
