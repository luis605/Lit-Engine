module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Engine.Portal;

import Engine.glm;

// Generated portal frames (docs/Portals.md, "Frames and edges"). Everything is in the portal's local space: x right, y up, z the
// normal (toward the end's own cell), the portal plane at z = 0.

namespace {

constexpr float kTwoPi = 6.28318530718f;
constexpr int kCornerSegments = 8;
constexpr int kEllipseSegments = 72;

struct ContourPoint {
    glm::vec2 position;
    glm::vec2 normal;  // outward
};

// Rounded rectangle (radius 0 = rectangle with sharp corners: each corner is a fan of coincident points with turning normals,
// so offsetting it gives the exact rounded offset and straight edges keep a constant normal).
std::vector<ContourPoint> roundedContour(const glm::vec2& half, float radius, float offset) {
    std::vector<ContourPoint> points;
    points.reserve(4 * (kCornerSegments + 1));
    radius = std::clamp(radius, 0.0f, std::min(half.x, half.y));
    const std::array<glm::vec2, 4> signs = {glm::vec2(1.0f, 1.0f), glm::vec2(-1.0f, 1.0f), glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f)};
    for (int corner = 0; corner < 4; ++corner) {
        const glm::vec2 center = (half - glm::vec2(radius)) * signs[static_cast<size_t>(corner)];
        for (int segment = 0; segment <= kCornerSegments; ++segment) {
            const float angle = (static_cast<float>(corner) + static_cast<float>(segment) / static_cast<float>(kCornerSegments)) * kTwoPi * 0.25f;
            const glm::vec2 normal(std::cos(angle), std::sin(angle));
            points.push_back({center + normal * (radius + offset), normal});
        }
    }
    return points;
}

std::vector<ContourPoint> ellipseContour(const glm::vec2& half, float offset) {
    std::vector<ContourPoint> points;
    points.reserve(kEllipseSegments);
    for (int segment = 0; segment < kEllipseSegments; ++segment) {
        const float angle = static_cast<float>(segment) / static_cast<float>(kEllipseSegments) * kTwoPi;
        const glm::vec2 onCurve(half.x * std::cos(angle), half.y * std::sin(angle));
        const glm::vec2 normal = glm::normalize(glm::vec2(std::cos(angle) / std::max(half.x, 1.0e-4f), std::sin(angle) / std::max(half.y, 1.0e-4f)));
        points.push_back({onCurve + normal * offset, normal});
    }
    return points;
}

std::vector<ContourPoint> innerContour(const glm::vec2& half, const PortalLook& look, float offset) {
    if (look.shape == PortalShape::Ellipse) return ellipseContour(half, offset);
    return roundedContour(half, look.shape == PortalShape::Rounded ? look.cornerRadius : 0.0f, offset);
}

// Outer edge of the frame: a rounded rectangle `width` outside the quad (it also covers a rectangular wall opening around an
// elliptical portal). Its radius is at least the bevel so the inset bevel stays a proper offset.
std::vector<ContourPoint> outerContour(const glm::vec2& half, const PortalLook& look, float offset) {
    const PortalFrameStyle& frame = look.frame;
    float radius = frame.bevel;
    if (look.shape == PortalShape::Rounded) radius = std::clamp(look.cornerRadius, 0.0f, std::min(half.x, half.y)) + frame.width;
    if (look.shape == PortalShape::Ellipse) radius = std::min(3.0f * frame.width, std::min(half.x, half.y) + frame.width);
    radius = std::max(radius, frame.bevel + 1.0e-3f);
    return roundedContour(half + glm::vec2(frame.width), radius, offset);
}

class MeshWriter {
  public:
    explicit MeshWriter(PortalFrameMesh& mesh) : m_mesh(mesh) {}

    unsigned int vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv) {
        const unsigned int index = static_cast<unsigned int>(m_mesh.vertices.size() / 8);
        m_mesh.vertices.insert(m_mesh.vertices.end(), {position.x, position.y, position.z, normal.x, normal.y, normal.z, uv.x, uv.y});
        return index;
    }

    // Triangle wound counter-clockwise around its vertices' mean normal (degenerate triangles are kept, they rasterise nothing).
    void triangle(unsigned int a, unsigned int b, unsigned int c) {
        const glm::vec3 pa = position(a);
        const glm::vec3 pb = position(b);
        const glm::vec3 pc = position(c);
        const glm::vec3 meanNormal = normal(a) + normal(b) + normal(c);
        if (glm::dot(glm::cross(pb - pa, pc - pa), meanNormal) < 0.0f) std::swap(b, c);
        m_mesh.indices.insert(m_mesh.indices.end(), {a, b, c});
    }

    // Strip between two contours with the same structure (point i of one belongs to point i of the other).
    void strip(const std::vector<ContourPoint>& first, float firstZ, const std::vector<ContourPoint>& second, float secondZ, const std::function<glm::vec3(const glm::vec2&)>& normalOf) {
        const size_t count = std::min(first.size(), second.size());
        if (count < 2) return;
        std::vector<unsigned int> top;
        std::vector<unsigned int> bottom;
        for (size_t index = 0; index <= count; ++index) {
            const size_t wrapped = index % count;
            const float u = static_cast<float>(index) / static_cast<float>(count);
            const glm::vec3 n = normalOf(first[wrapped].normal);
            top.push_back(vertex(glm::vec3(first[wrapped].position, firstZ), n, glm::vec2(u, firstZ)));
            bottom.push_back(vertex(glm::vec3(second[wrapped].position, secondZ), n, glm::vec2(u, secondZ)));
        }
        for (size_t index = 0; index < count; ++index) {
            triangle(top[index], bottom[index], bottom[index + 1]);
            triangle(top[index], bottom[index + 1], top[index + 1]);
        }
    }

    // Flat ring between two closed star-shaped contours (front face): zipper by polar angle.
    void ring(const std::vector<ContourPoint>& inner, const std::vector<ContourPoint>& outer, float z) {
        if (inner.size() < 3 || outer.size() < 3) return;
        auto prepare = [&](const std::vector<ContourPoint>& contour, std::vector<unsigned int>& ids, std::vector<float>& angles) {
            size_t start = 0;
            float smallest = 1.0e30f;
            for (size_t index = 0; index < contour.size(); ++index) {
                float angle = std::atan2(contour[index].position.y, contour[index].position.x);
                if (angle < 0.0f) angle += kTwoPi;
                if (angle < smallest) {
                    smallest = angle;
                    start = index;
                }
            }
            float previous = -1.0f;
            float turn = 0.0f;
            for (size_t step = 0; step <= contour.size(); ++step) {
                const ContourPoint& point = contour[(start + step) % contour.size()];
                float angle = std::atan2(point.position.y, point.position.x);
                if (angle < 0.0f) angle += kTwoPi;
                if (step == contour.size()) angle = angles.front() + kTwoPi;
                else {
                    if (previous >= 0.0f && angle + turn < previous - 1.0e-4f) turn += kTwoPi;
                    angle += turn;
                }
                previous = angle;
                angles.push_back(angle);
                ids.push_back(vertex(glm::vec3(point.position, z), glm::vec3(0.0f, 0.0f, 1.0f), point.position));
            }
        };
        std::vector<unsigned int> innerIds;
        std::vector<unsigned int> outerIds;
        std::vector<float> innerAngles;
        std::vector<float> outerAngles;
        prepare(inner, innerIds, innerAngles);
        prepare(outer, outerIds, outerAngles);
        // Both lists start at their smallest angle; align the starts by sharing the first triangle fan.
        size_t i = 0;
        size_t o = 0;
        const size_t innerLast = innerIds.size() - 1;
        const size_t outerLast = outerIds.size() - 1;
        while (i < innerLast || o < outerLast) {
            const bool advanceInner = o >= outerLast || (i < innerLast && innerAngles[i + 1] <= outerAngles[o + 1]);
            if (advanceInner) {
                triangle(innerIds[i], outerIds[o], innerIds[i + 1]);
                ++i;
            } else {
                triangle(innerIds[i], outerIds[o], outerIds[o + 1]);
                ++o;
            }
        }
    }

  private:
    glm::vec3 position(unsigned int index) const { return glm::vec3(m_mesh.vertices[index * 8], m_mesh.vertices[index * 8 + 1], m_mesh.vertices[index * 8 + 2]); }
    glm::vec3 normal(unsigned int index) const { return glm::vec3(m_mesh.vertices[index * 8 + 3], m_mesh.vertices[index * 8 + 4], m_mesh.vertices[index * 8 + 5]); }
    PortalFrameMesh& m_mesh;
};

}

void buildPortalFrameMesh(const glm::vec2& half, const PortalLook& look, PortalFrameMesh& body, PortalFrameMesh& reveal) {
    body = {};
    reveal = {};
    const PortalFrameStyle& frame = look.frame;
    const float bevel = std::clamp(frame.bevel, 0.0f, std::min(frame.width, frame.depth) * 0.45f);
    const float front = frame.depth;
    const float lip = front - bevel;
    const float back = -frame.back;
    constexpr float kInvSqrt2 = 0.70710678f;
    MeshWriter bodyWriter(body);
    MeshWriter revealWriter(reveal);
    const std::vector<ContourPoint> innerFront = innerContour(half, look, bevel);
    const std::vector<ContourPoint> innerLip = innerContour(half, look, 0.0f);
    const std::vector<ContourPoint> outerFront = outerContour(half, look, -bevel);
    const std::vector<ContourPoint> outerLip = outerContour(half, look, 0.0f);
    bodyWriter.ring(innerFront, outerFront, front);
    bodyWriter.strip(innerFront, front, innerLip, lip, [](const glm::vec2& n) { return glm::vec3(-n.x * kInvSqrt2, -n.y * kInvSqrt2, kInvSqrt2); });
    bodyWriter.strip(outerFront, front, outerLip, lip, [](const glm::vec2& n) { return glm::vec3(n.x * kInvSqrt2, n.y * kInvSqrt2, kInvSqrt2); });
    bodyWriter.strip(outerLip, lip, outerLip, back, [](const glm::vec2& n) { return glm::vec3(n, 0.0f); });
    revealWriter.strip(innerLip, lip, innerLip, back, [](const glm::vec2& n) { return glm::vec3(-n, 0.0f); });
}

std::string portalFrameKey(const glm::vec2& half, const PortalLook& look) {
    const PortalFrameStyle& frame = look.frame;
    return std::format("portal-frame {} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {:.4f}", portalShapeName(look.shape), half.x, half.y, look.shape == PortalShape::Rounded ? look.cornerRadius : 0.0f, frame.width, frame.depth, frame.back, frame.bevel);
}
