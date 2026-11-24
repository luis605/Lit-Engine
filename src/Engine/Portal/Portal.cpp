module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
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
constexpr float kMinFrustumPlaneLength = 1.0e-8f;
constexpr float kMinObliqueDenominator = 1.0e-8f;
}

glm::mat4 portalPose(const glm::vec3& position, const glm::vec3& eulerDegrees) {
    glm::mat4 pose = glm::translate(glm::mat4(1.0f), position);
    pose = glm::rotate(pose, glm::radians(eulerDegrees.x), glm::vec3(0.0f, 1.0f, 0.0f));
    pose = glm::rotate(pose, glm::radians(eulerDegrees.y), glm::vec3(1.0f, 0.0f, 0.0f));
    pose = glm::rotate(pose, glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    return pose;
}

glm::mat4 obliqueProjection(const glm::mat4& projection, const glm::mat4& view, const glm::vec4& worldPlane) {
    const glm::mat4 inverseView = glm::inverse(view);
    const glm::vec4 viewPlane(glm::dot(inverseView[0], worldPlane), glm::dot(inverseView[1], worldPlane), glm::dot(inverseView[2], worldPlane), glm::dot(inverseView[3], worldPlane));
    // Reversed Z (near = 1, far = 0): the clip depth row becomes w - scale * plane, so points on the plane map to depth 1 and the far corner to 0.
    const glm::vec4 corner = glm::inverse(projection) * glm::vec4(viewPlane.x < 0.0f ? -1.0f : 1.0f, viewPlane.y < 0.0f ? -1.0f : 1.0f, 0.0f, 1.0f);
    const float denominator = glm::dot(viewPlane, corner);
    if (std::abs(denominator) < kMinObliqueDenominator) return projection;
    const float scale = 1.0f / denominator;
    glm::mat4 result = projection;
    result[0][2] = projection[0][3] - viewPlane.x * scale;
    result[1][2] = projection[1][3] - viewPlane.y * scale;
    result[2][2] = projection[2][3] - viewPlane.z * scale;
    result[3][2] = projection[3][3] - viewPlane.w * scale;
    return result;
}

std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4& viewProjection) {
    const glm::vec4 row0(viewProjection[0][0], viewProjection[1][0], viewProjection[2][0], viewProjection[3][0]);
    const glm::vec4 row1(viewProjection[0][1], viewProjection[1][1], viewProjection[2][1], viewProjection[3][1]);
    const glm::vec4 row2(viewProjection[0][2], viewProjection[1][2], viewProjection[2][2], viewProjection[3][2]);
    const glm::vec4 row3(viewProjection[0][3], viewProjection[1][3], viewProjection[2][3], viewProjection[3][3]);
    std::array<glm::vec4, 6> planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};
    for (glm::vec4& plane : planes) {
        const float length = glm::length(glm::vec3(plane));
        if (length > kMinFrustumPlaneLength) plane = plane / length;
    }
    return planes;
}

// Any rigid transform (rolled, pitched, floor / ceiling, flipped portals): the camera frame takes the rotation, so the view
// matrix after the swap is the virtual camera the portal view was rendered from (docs/Portals.md, "Camera crossing").
void applyTransformToCamera(Camera& camera, const glm::mat4& transform) { camera.transformBy(transform); }

void PortalSystem::clear() {
    m_cells.clear();
    m_portals.clear();
    m_cellPortals.clear();
    m_cameraCell = -1;
    m_hasNear = false;
    m_tracked.clear();
}

int PortalSystem::addCell(const PortalCell& cell) {
    m_cells.push_back(cell);
    m_cellPortals.emplace_back();
    if (m_cameraCell < 0) m_cameraCell = 0;
    return static_cast<int>(m_cells.size()) - 1;
}

int PortalSystem::findCell(const std::string& name) const {
    for (size_t index = 0; index < m_cells.size(); ++index) {
        if (m_cells[index].name == name) return static_cast<int>(index);
    }
    return -1;
}

int PortalSystem::cellAt(const glm::vec3& position) const {
    for (size_t index = 0; index < m_cells.size(); ++index) {
        if (cellContains(m_cells[index], position)) return static_cast<int>(index);
    }
    return -1;
}

int PortalSystem::resolveCell(int cell, const glm::vec3& position) const {
    if (cell >= 0 && static_cast<size_t>(cell) < m_cells.size() && cellContains(m_cells[static_cast<size_t>(cell)], position)) return cell;
    const int found = cellAt(position);
    return found >= 0 ? found : cell;
}

void PortalSystem::setCameraCell(int cell) {
    m_cameraCell = cell;
    m_hasNear = false;
}

const std::vector<int>& PortalSystem::portalsOf(int cell) const {
    static const std::vector<int> none;
    return cell >= 0 && static_cast<size_t>(cell) < m_cellPortals.size() ? m_cellPortals[static_cast<size_t>(cell)] : none;
}

const char* portalShapeName(PortalShape shape) {
    switch (shape) {
        case PortalShape::Ellipse: return "ellipse";
        case PortalShape::Rounded: return "rounded";
        case PortalShape::Rect: break;
    }
    return "rect";
}

float portalShapeDistance(PortalShape shape, const glm::vec2& half, float cornerRadius, float u, float v) {
    const float au = std::abs(u);
    const float av = std::abs(v);
    if (shape == PortalShape::Ellipse) {
        // First-order distance (value over gradient length): exact on the contour, which is all the inside test needs.
        const glm::vec2 h = glm::max(half, glm::vec2(1.0e-4f));
        const float k = std::sqrt((au * au) / (h.x * h.x) + (av * av) / (h.y * h.y));
        const float g = std::sqrt((au * au) / (h.x * h.x * h.x * h.x) + (av * av) / (h.y * h.y * h.y * h.y));
        return g > 1.0e-6f ? k * (k - 1.0f) / g : -std::min(h.x, h.y);
    }
    const float radius = shape == PortalShape::Rounded ? std::clamp(cornerRadius, 0.0f, std::min(half.x, half.y)) : 0.0f;
    const float qx = au - half.x + radius;
    const float qy = av - half.y + radius;
    const float outside = std::sqrt(std::max(qx, 0.0f) * std::max(qx, 0.0f) + std::max(qy, 0.0f) * std::max(qy, 0.0f));
    return outside + std::min(std::max(qx, qy), 0.0f) - radius;
}

glm::vec3 PortalSystem::cellUp(int cell) const {
    if (cell < 0 || static_cast<size_t>(cell) >= m_cells.size()) return glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 gravity = m_cells[static_cast<size_t>(cell)].params.gravity;
    const float length = glm::length(gravity);
    return length > 1.0e-4f ? -gravity / length : glm::vec3(0.0f, 1.0f, 0.0f);
}

void PortalSystem::setLook(int portal, const PortalLook& look) {
    if (portal < 0 || static_cast<size_t>(portal) >= m_portals.size()) return;
    m_portals[static_cast<size_t>(portal)].look = look;
    const int partner = m_portals[static_cast<size_t>(portal)].partner;
    if (partner >= 0 && static_cast<size_t>(partner) < m_portals.size()) m_portals[static_cast<size_t>(partner)].look = look;
}

glm::vec3 PortalSystem::spillIrradiance(int portal, const std::vector<glm::vec4>& packedLights, const std::vector<int32_t>& cells, const glm::vec3& sunDirection, const glm::vec3& sunIlluminance) const {
    if (portal < 0 || static_cast<size_t>(portal) >= m_portals.size()) return glm::vec3(0.0f);
    const Portal& source = m_portals[static_cast<size_t>(portal)];
    if (source.look.spill <= 0.0f || m_settings.spill <= 0.0f || source.partner < 0) return glm::vec3(0.0f);
    const Portal& far = m_portals[static_cast<size_t>(source.partner)];
    const glm::vec3 center = far.center();
    const glm::vec3 normal = far.normal();
    constexpr float kMinDistanceSquared = 0.25f;
    glm::vec3 irradiance(0.0f);
    const size_t count = std::min(packedLights.size() / 4, cells.size());
    for (size_t light = 0; light < count; ++light) {
        if (cells[light] != far.cell) continue;
        const glm::vec4 positionRange = packedLights[light * 4];
        const glm::vec4 colorIntensity = packedLights[light * 4 + 1];
        const glm::vec3 offset = glm::vec3(positionRange) - center;
        const float distanceSquared = glm::dot(offset, offset);
        const float range = positionRange.w;
        if (range <= 0.0f || distanceSquared >= range * range) continue;
        // Light that reaches the far room's surfaces around the opening: cosine to the far end's normal (lights behind it do not count),
        // smooth range window like the shaders.
        const float facing = std::max(glm::dot(normal, offset) / std::sqrt(std::max(distanceSquared, 1.0e-8f)), 0.0f);
        const float ratio = distanceSquared / (range * range);
        const float window = (1.0f - ratio * ratio) * (1.0f - ratio * ratio);
        irradiance += glm::vec3(colorIntensity) * colorIntensity.w * (0.5f + 0.5f * facing) * window / std::max(distanceSquared, kMinDistanceSquared);
    }
    const PortalCell& farCell = m_cells[static_cast<size_t>(far.cell)];
    if (farCell.params.sun > 0.0f && glm::dot(sunDirection, sunDirection) > 0.0f) irradiance += sunIlluminance * farCell.params.sun * 0.25f;
    return irradiance * source.look.spill * std::max(m_settings.spill, 0.0f);
}

int PortalSystem::addPair(const std::string& name, const PortalEnd& endA, const PortalEnd& endB, float twistDegrees, const PortalLook& look) {
    const int cellA = findCell(endA.cell);
    const int cellB = findCell(endB.cell);
    if (cellA < 0 || cellB < 0) return -1;
    Portal portalA;
    Portal portalB;
    portalA.name = name + ".a";
    portalB.name = name + ".b";
    portalA.cell = cellA;
    portalB.cell = cellB;
    portalA.pose = portalPose(endA.position, endA.eulerDegrees);
    portalB.pose = portalPose(endB.position, endB.eulerDegrees);
    portalA.half = endA.halfSize;
    portalA.euler = endA.eulerDegrees;
    portalB.euler = endB.eulerDegrees;
    portalA.look = look;
    portalB.look = look;
    portalA.twist = twistDegrees;
    portalB.twist = -twistDegrees;
    portalB.half = endB.halfSize;
    const glm::mat4 flip = glm::rotate(glm::mat4(1.0f), kPortalPi, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 twist = glm::rotate(glm::mat4(1.0f), glm::radians(twistDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    portalA.transform = portalB.pose * twist * flip * glm::inverse(portalA.pose);
    portalB.transform = glm::inverse(portalA.transform);
    const int indexA = static_cast<int>(m_portals.size());
    portalA.partner = indexA + 1;
    portalB.partner = indexA;
    m_portals.push_back(std::move(portalA));
    m_portals.push_back(std::move(portalB));
    m_cellPortals[static_cast<size_t>(cellA)].push_back(indexA);
    m_cellPortals[static_cast<size_t>(cellB)].push_back(indexA + 1);
    return indexA;
}
