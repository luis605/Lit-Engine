module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
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

constexpr size_t kHardViewCap = 24;
constexpr float kRootPixels = 1.0e30f;
constexpr uint64_t kKeyBasis = 1469598103934665603ull;
constexpr uint64_t kKeyPrime = 1099511628211ull;
constexpr float kLargePortalHeightFraction = 0.2f;
constexpr float kNearReachFactor = 1.5f;
constexpr float kObliqueEntryDistance = -0.01f;
constexpr float kMaskedEntryDistance = -0.02f;
constexpr float kOcclusionSampleInset = 0.8f;
constexpr float kMinDistance = 1.0e-3f;
constexpr float kMinProjectionScale = 1.0e-4f;

struct PendingView {
    int node;
    glm::mat4 cameraWorld;
    int entryPortal;
    float pixels;
};

struct Candidate {
    int portalId;
    float pixels;
};

class FrameBuilder {
  public:
    FrameBuilder(const std::vector<Portal>& portals, const std::vector<PortalCell>& cells, const std::vector<std::vector<int>>& cellPortals, const PortalSettings& settings, const PortalBlockedFn& blocked, const Camera& camera, float viewportHeight)
        : m_portals(portals), m_cells(cells), m_cellPortals(cellPortals), m_settings(settings), m_blocked(blocked), m_viewportHeight(viewportHeight), m_projection(camera.getProjectionMatrix()), m_rootView(camera.getViewMatrix()), m_farPlane(camera.getFarPlane()) {
        m_tanHalf = 1.0f / std::max(m_projection[1][1], kMinProjectionScale);
        const float aspect = m_projection[1][1] / std::max(m_projection[0][0], kMinProjectionScale);
        m_reach = camera.getNearPlane() * std::sqrt(1.0f + m_tanHalf * m_tanHalf * (1.0f + aspect * aspect));
        m_maxViews = static_cast<size_t>(std::max(settings.maxViews, 2));
        m_cameraPosition = camera.getPosition();
    }

    PortalFrame build(int cameraCell) {
        m_frame.nodes.reserve(m_maxViews);
        addRoot(cameraCell);
        m_queue.push_back({0, glm::inverse(m_rootView), -1, kRootPixels});
        for (size_t queueIndex = 0; queueIndex < m_queue.size(); ++queueIndex) {
            const size_t best = static_cast<size_t>(std::ranges::max_element(m_queue.begin() + static_cast<std::ptrdiff_t>(queueIndex), m_queue.end(), {}, &PendingView::pixels) - m_queue.begin());
            std::swap(m_queue[queueIndex], m_queue[best]);
            expand(m_queue[queueIndex]);
        }
        return std::move(m_frame);
    }

  private:
    const std::vector<Portal>& m_portals;
    const std::vector<PortalCell>& m_cells;
    const std::vector<std::vector<int>>& m_cellPortals;
    const PortalSettings& m_settings;
    const PortalBlockedFn& m_blocked;
    float m_viewportHeight;
    glm::mat4 m_projection;
    glm::mat4 m_rootView;
    float m_farPlane;
    float m_tanHalf = 1.0f;
    float m_reach = 0.0f;
    size_t m_maxViews = 2;
    glm::vec3 m_cameraPosition{0.0f};
    PortalFrame m_frame;
    std::vector<PendingView> m_queue;

    void fillNode(PortalViewNode& node, int cell) const {
        const PortalCell& source = m_cells[static_cast<size_t>(cell)];
        node.cell = cell;
        node.sunScale = source.params.sun;
        node.ambientScale = source.params.ambient;
        node.label = source.name;
    }

    void addRoot(int cameraCell) {
        PortalViewNode root;
        root.view = m_rootView;
        root.projection = m_projection;
        root.position = m_cameraPosition;
        root.planes = extractFrustumPlanes(m_projection * m_rootView);
        root.key = kKeyBasis ^ static_cast<uint64_t>(cameraCell + 1);
        fillNode(root, cameraCell);
        m_frame.nodes.push_back(std::move(root));
    }

    bool insideFrustum(const Portal& portal, const std::array<glm::vec4, 6>& planes) const {
        const std::array<glm::vec3, 4> corners = cornersOf(portal);
        for (const glm::vec4& plane : planes) {
            bool anyInside = false;
            for (const glm::vec3& corner : corners) anyInside = anyInside || glm::dot(glm::vec3(plane), corner) + plane.w >= 0.0f;
            if (!anyInside) return false;
        }
        return true;
    }

    bool occluded(const Portal& portal, const glm::vec3& eye) const {
        const glm::vec3 across = portal.right() * portal.half.x * kOcclusionSampleInset;
        const glm::vec3 upward = portal.up() * portal.half.y * kOcclusionSampleInset;
        const glm::vec3 center = portal.center();
        const std::array<glm::vec3, 5> samples = {center, center + across + upward, center - across + upward, center + across - upward, center - across - upward};
        for (const glm::vec3& sample : samples) {
            if (!m_blocked(eye, sample)) return false;
        }
        return true;
    }

    std::vector<Candidate> collectCandidates(const PendingView& current) const {
        const PortalViewNode& node = m_frame.nodes[static_cast<size_t>(current.node)];
        const glm::vec3 eye(current.cameraWorld[3]);
        std::vector<Candidate> candidates;
        if (node.cell < 0 || static_cast<size_t>(node.cell) >= m_cellPortals.size()) return candidates;
        for (int portalId : m_cellPortals[static_cast<size_t>(node.cell)]) {
            if (portalId == current.entryPortal) continue;
            const Portal& portal = m_portals[static_cast<size_t>(portalId)];
            const glm::vec3 offset = eye - portal.center();
            const float planeDistance = glm::dot(portal.normal(), offset);
            if (planeDistance < -m_reach) continue;
            if (!insideFrustum(portal, node.planes)) continue;
            const float distance = std::max(glm::length(offset), kMinDistance);
            const float radius = glm::length(glm::vec3(portal.half.x, portal.half.y, 0.0f));
            const float pixels = std::min(m_viewportHeight * radius / (distance * m_tanHalf), current.pixels);
            if (m_blocked && m_settings.occlusion && planeDistance > m_reach && occluded(portal, eye)) continue;
            candidates.push_back({portalId, pixels});
        }
        std::ranges::stable_sort(candidates, [](const Candidate& left, const Candidate& right) { return left.pixels > right.pixels; });
        return candidates;
    }

    void setMaskedPlanes(PortalViewNode& child, const Portal& destination, const glm::vec3& childEye, const glm::mat4& childCameraWorld, const glm::vec4& clipPlane) const {
        child.masked = true;
        child.entryCenter = destination.center();
        child.entryAxisU = destination.right() * destination.half.x;
        child.entryAxisV = destination.up() * destination.half.y;
        child.entryNormal = destination.normal();
        const std::array<glm::vec3, 4> corners = cornersOf(destination);
        const glm::vec3 destinationCenter = destination.center();
        for (size_t side = 0; side < corners.size(); ++side) {
            glm::vec3 planeNormal = glm::normalize(glm::cross(corners[side] - childEye, corners[(side + 1) % corners.size()] - childEye));
            if (glm::dot(planeNormal, destinationCenter - childEye) < 0.0f) planeNormal = -planeNormal;
            child.planes[side] = glm::vec4(planeNormal, -glm::dot(planeNormal, childEye));
        }
        child.planes[4] = clipPlane;
        const glm::vec3 forward = glm::normalize(-glm::vec3(childCameraWorld[2]));
        child.planes[5] = glm::vec4(-forward, glm::dot(forward, childEye) + m_farPlane);
    }

    PortalViewNode makeChild(const PendingView& current, int portalId, const Portal& destination, const glm::mat4& childCameraWorld) const {
        const PortalViewNode& parent = m_frame.nodes[static_cast<size_t>(current.node)];
        const glm::mat4 childView = glm::inverse(childCameraWorld);
        const glm::vec3 childEye(childCameraWorld[3]);
        const glm::vec3 destinationNormal = destination.normal();
        const float eyeDistance = glm::dot(destinationNormal, childEye - destination.center());
        PortalViewNode child;
        child.view = childView;
        child.position = childEye;
        child.depth = parent.depth + 1;
        child.key = (parent.key ^ static_cast<uint64_t>(portalId + 1)) * kKeyPrime;
        fillNode(child, destination.cell);
        child.label = parent.label + ">" + child.label;
        const glm::vec4 clipPlane(destinationNormal, -glm::dot(destinationNormal, destination.center()));
        child.projection = eyeDistance < kObliqueEntryDistance ? obliqueProjection(m_projection, childView, clipPlane) : m_projection;
        if (eyeDistance < kMaskedEntryDistance) setMaskedPlanes(child, destination, childEye, childCameraWorld, clipPlane);
        else child.planes = extractFrustumPlanes(m_projection * childView);
        return child;
    }

    PortalSurface makeSurface(const Portal& source, const Portal& destination) const {
        PortalSurface surface;
        surface.center = source.center();
        surface.axisU = source.right() * source.half.x;
        surface.axisV = source.up() * source.half.y;
        surface.normal = source.normal();
        surface.fallback = m_cells[static_cast<size_t>(destination.cell)].params.tint;
        return surface;
    }

    bool shouldDescend(const PendingView& current, const Candidate& candidate, const Portal& source) const {
        const int depth = m_frame.nodes[static_cast<size_t>(current.node)].depth;
        const glm::vec3 eye(current.cameraWorld[3]);
        const float eyeDistance = glm::dot(source.normal(), eye - source.center());
        const bool near = eyeDistance < m_reach * kNearReachFactor;
        const bool large = candidate.pixels >= m_viewportHeight * kLargePortalHeightFraction;
        const size_t cap = large ? kHardViewCap : m_maxViews;
        return depth < m_settings.maxDepth + (large ? 1 : 0) && m_frame.nodes.size() < cap && (candidate.pixels >= m_settings.minPixels || near);
    }

    void expand(const PendingView current) {
        const std::vector<Candidate> candidates = collectCandidates(current);
        for (const Candidate& candidate : candidates) {
            const Portal& source = m_portals[static_cast<size_t>(candidate.portalId)];
            const Portal& destination = m_portals[static_cast<size_t>(source.partner)];
            PortalSurface surface = makeSurface(source, destination);
            if (shouldDescend(current, candidate, source)) {
                const glm::mat4 childCameraWorld = source.transform * current.cameraWorld;
                surface.child = static_cast<int>(m_frame.nodes.size());
                PortalViewNode child = makeChild(current, candidate.portalId, destination, childCameraWorld);
                m_frame.nodes.push_back(std::move(child));
                m_queue.push_back({surface.child, childCameraWorld, source.partner, candidate.pixels});
            }
            m_frame.nodes[static_cast<size_t>(current.node)].surfaces.push_back(surface);
        }
    }
};

}

PortalFrame PortalSystem::buildFrame(const Camera& camera, float viewportHeight, const PortalBlockedFn& blocked) const {
    if (m_portals.empty() || m_cameraCell < 0 || static_cast<size_t>(m_cameraCell) >= m_cells.size()) return {};
    FrameBuilder builder(m_portals, m_cells, m_cellPortals, m_settings, blocked, camera, viewportHeight);
    return builder.build(m_cameraCell);
}
