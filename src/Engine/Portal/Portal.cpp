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
    const glm::vec4 corner = glm::inverse(projection) * glm::vec4(viewPlane.x < 0.0f ? -1.0f : 1.0f, viewPlane.y < 0.0f ? -1.0f : 1.0f, 1.0f, 1.0f);
    const float denominator = glm::dot(viewPlane, corner);
    if (std::abs(denominator) < kMinObliqueDenominator) return projection;
    const float scale = 1.0f / denominator;
    glm::mat4 result = projection;
    result[0][2] = viewPlane.x * scale;
    result[1][2] = viewPlane.y * scale;
    result[2][2] = viewPlane.z * scale;
    result[3][2] = viewPlane.w * scale;
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

void applyTransformToCamera(Camera& camera, const glm::mat4& transform) {
    const glm::vec3 front = glm::normalize(transformDirection(transform, cameraForward(camera)));
    camera.setPos(transformPoint(transform, camera.getPosition()));
    camera.setOrientation(glm::degrees(std::atan2(front.z, front.x)), glm::degrees(std::asin(glm::clamp(front.y, -1.0f, 1.0f))));
}

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

int PortalSystem::addPair(const std::string& name, const PortalEnd& endA, const PortalEnd& endB, float twistDegrees) {
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
