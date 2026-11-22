module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

module Engine.Physics;

#include "PhysicsShapes.hpp"

using namespace phys_detail;

namespace {
constexpr uint32_t kLeafTriangles = 4;
constexpr int kMaxHullPoints = 128;

Triangle triangleAt(const TriangleMeshData& mesh, uint32_t index) {
    return {mesh.vertices[mesh.indices[index * 3]], mesh.vertices[mesh.indices[index * 3 + 1]], mesh.vertices[mesh.indices[index * 3 + 2]]};
}

struct SweepHit {
    float t = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

std::optional<SweepHit> sweepSphereTriangle(const glm::vec3& origin, const glm::vec3& dir, float maxDistance, float radius, const Triangle& tri) {
    const glm::vec3 triNormal = triangleNormal(tri);
    {
        const glm::vec3 cp = closestOnTriangle(origin, tri);
        const glm::vec3 away = origin - cp;
        const float d = glm::length(away);
        if (d < radius) return SweepHit{0.0f, d > 1.0e-6f ? away / d : (glm::dot(origin - tri.a, triNormal) >= 0.0f ? triNormal : -triNormal)};
    }
    float best = std::numeric_limits<float>::max();
    const float side = glm::dot(origin - tri.a, triNormal);
    const glm::vec3 faceNormal = side >= 0.0f ? triNormal : -triNormal;
    const float denominator = glm::dot(dir, faceNormal);
    if (denominator < -1.0e-7f) {
        const float t = (radius - std::abs(side)) / denominator;
        if (t >= 0.0f && t <= maxDistance) {
            const glm::vec3 onPlane = origin + dir * t - faceNormal * radius;
            if (glm::length(closestOnTriangle(onPlane, tri) - onPlane) < 1.0e-4f) best = t;
        }
    }
    const glm::vec3 vertices[3] = {tri.a, tri.b, tri.c};
    for (int i = 0; i < 3; ++i) {
        const glm::vec3 a = vertices[i];
        const glm::vec3 b = vertices[(i + 1) % 3];
        const glm::vec3 e = b - a;
        const float length = glm::length(e);
        if (length < 1.0e-8f) continue;
        const glm::vec3 u = e / length;
        const glm::vec3 m = origin - a;
        const glm::vec3 dPerp = dir - u * glm::dot(dir, u);
        const glm::vec3 mPerp = m - u * glm::dot(m, u);
        const float qa = glm::dot(dPerp, dPerp);
        if (qa < 1.0e-10f) continue;
        const float qb = glm::dot(dPerp, mPerp);
        const float qc = glm::dot(mPerp, mPerp) - radius * radius;
        const float disc = qb * qb - qa * qc;
        if (disc < 0.0f) continue;
        const float t = (-qb - std::sqrt(disc)) / qa;
        if (t < 0.0f || t > maxDistance || t >= best) continue;
        const float along = glm::dot(m + dir * t, u);
        if (along >= 0.0f && along <= length) best = t;
    }
    for (const glm::vec3& v : vertices) {
        const glm::vec3 m = origin - v;
        const float b = glm::dot(m, dir);
        const float c = glm::dot(m, m) - radius * radius;
        if (c > 0.0f && b > 0.0f) continue;
        const float disc = b * b - c;
        if (disc < 0.0f) continue;
        const float t = std::max(0.0f, -b - std::sqrt(disc));
        if (t <= maxDistance && t < best) best = t;
    }
    if (best == std::numeric_limits<float>::max()) return std::nullopt;
    const glm::vec3 p = origin + dir * best;
    const glm::vec3 away = p - closestOnTriangle(p, tri);
    const float d = glm::length(away);
    return SweepHit{best, d > 1.0e-6f ? away / d : faceNormal};
}
}

void TriangleMeshData::build() {
    nodes.clear();
    order.clear();
    const uint32_t count = static_cast<uint32_t>(triangleCount());
    if (count == 0) return;
    order.resize(count);
    std::iota(order.begin(), order.end(), 0u);
    std::vector<glm::vec3> centroids(count);
    for (uint32_t i = 0; i < count; ++i) {
        const Triangle t = triangleAt(*this, i);
        centroids[i] = (t.a + t.b + t.c) / 3.0f;
    }
    nodes.reserve(count * 2);
    nodes.emplace_back();
    struct Range {
        uint32_t node, begin, end;
    };
    std::vector<Range> stack{{0, 0, count}};
    while (!stack.empty()) {
        const Range range = stack.back();
        stack.pop_back();
        glm::vec3 lo(1.0e30f), hi(-1.0e30f), clo(1.0e30f), chi(-1.0e30f);
        for (uint32_t i = range.begin; i < range.end; ++i) {
            const Triangle t = triangleAt(*this, order[i]);
            for (const glm::vec3& p : {t.a, t.b, t.c}) {
                lo = glm::min(lo, p);
                hi = glm::max(hi, p);
            }
            clo = glm::min(clo, centroids[order[i]]);
            chi = glm::max(chi, centroids[order[i]]);
        }
        nodes[range.node].lo = lo;
        nodes[range.node].hi = hi;
        if (range.end - range.begin <= kLeafTriangles) {
            nodes[range.node].first = range.begin;
            nodes[range.node].count = range.end - range.begin;
            continue;
        }
        const glm::vec3 extent = chi - clo;
        const int axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : (extent.y >= extent.z ? 1 : 2);
        const uint32_t mid = (range.begin + range.end) / 2;
        std::nth_element(order.begin() + range.begin, order.begin() + mid, order.begin() + range.end, [&](uint32_t l, uint32_t r) { return centroids[l][axis] < centroids[r][axis]; });
        const uint32_t left = static_cast<uint32_t>(nodes.size());
        nodes[range.node].left = left;
        nodes[range.node].count = 0;
        nodes.emplace_back();
        nodes.emplace_back();
        stack.push_back({left, range.begin, mid});
        stack.push_back({left + 1, mid, range.end});
    }
}

std::shared_ptr<const TriangleMeshData> makeTriangleMesh(std::vector<glm::vec3> vertices, std::vector<uint32_t> indices) {
    auto mesh = std::make_shared<TriangleMeshData>();
    indices.resize(indices.size() / 3 * 3);
    for (uint32_t index : indices) {
        if (index >= vertices.size()) return nullptr;
    }
    mesh->vertices = std::move(vertices);
    mesh->indices = std::move(indices);
    mesh->build();
    return mesh;
}

void meshQuery(const TriangleMeshData& mesh, const glm::vec3& lo, const glm::vec3& hi, std::vector<uint32_t>& triangles) {
    if (!mesh.valid()) return;
    std::vector<uint32_t> stack{0};
    while (!stack.empty()) {
        const TriangleMeshData::Node& node = mesh.nodes[stack.back()];
        stack.pop_back();
        if (node.lo.x > hi.x || node.hi.x < lo.x || node.lo.y > hi.y || node.hi.y < lo.y || node.lo.z > hi.z || node.hi.z < lo.z) continue;
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) triangles.push_back(mesh.order[node.first + i]);
        } else {
            stack.push_back(node.left);
            stack.push_back(node.left + 1);
        }
    }
}

std::optional<MeshHit> meshRaycast(const TriangleMeshData& mesh, const glm::vec3& origin, const glm::vec3& direction, float maxDistance) {
    if (!mesh.valid()) return std::nullopt;
    const float length = glm::length(direction);
    if (length < kEps) return std::nullopt;
    const glm::vec3 dir = direction / length;
    glm::vec3 inv;
    for (int axis = 0; axis < 3; ++axis) inv[axis] = std::abs(dir[axis]) < 1.0e-12f ? 1.0e12f : 1.0f / dir[axis];
    std::optional<MeshHit> best;
    float bestT = maxDistance;
    std::vector<uint32_t> stack{0};
    while (!stack.empty()) {
        const TriangleMeshData::Node& node = mesh.nodes[stack.back()];
        stack.pop_back();
        float enter = 0.0f;
        float leave = bestT;
        bool miss = false;
        for (int axis = 0; axis < 3 && !miss; ++axis) {
            if (std::abs(dir[axis]) < 1.0e-12f) {
                if (origin[axis] < node.lo[axis] - 1.0e-5f || origin[axis] > node.hi[axis] + 1.0e-5f) miss = true;
                continue;
            }
            float t0 = (node.lo[axis] - origin[axis]) * inv[axis];
            float t1 = (node.hi[axis] - origin[axis]) * inv[axis];
            if (t0 > t1) std::swap(t0, t1);
            enter = std::max(enter, t0 - 1.0e-5f);
            leave = std::min(leave, t1 + 1.0e-5f);
            if (enter > leave) miss = true;
        }
        if (miss) continue;
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                const uint32_t tri = mesh.order[node.first + i];
                const Triangle t = triangleAt(mesh, tri);
                const auto hit = rayTriangle(origin, dir, t);
                if (hit && *hit <= bestT) {
                    bestT = *hit;
                    glm::vec3 n = triangleNormal(t);
                    if (glm::dot(n, dir) > 0.0f) n = -n;
                    best = MeshHit{*hit, n, tri};
                }
            }
        } else {
            stack.push_back(node.left);
            stack.push_back(node.left + 1);
        }
    }
    return best;
}

std::optional<MeshHit> meshSweepSphere(const TriangleMeshData& mesh, const glm::vec3& origin, const glm::vec3& direction, float maxDistance, float radius) {
    if (!mesh.valid()) return std::nullopt;
    const float length = glm::length(direction);
    if (length < kEps) return std::nullopt;
    const glm::vec3 dir = direction / length;
    const glm::vec3 end = origin + dir * maxDistance;
    std::vector<uint32_t> candidates;
    meshQuery(mesh, glm::min(origin, end) - radius, glm::max(origin, end) + radius, candidates);
    std::optional<MeshHit> best;
    for (uint32_t tri : candidates) {
        const auto hit = sweepSphereTriangle(origin, dir, maxDistance, radius, triangleAt(mesh, tri));
        if (hit && (!best || hit->t < best->distance)) best = MeshHit{hit->t, hit->normal, tri};
    }
    return best;
}

std::vector<MeshContact> meshSphereContacts(const TriangleMeshData& mesh, const glm::vec3& center, float radius) {
    std::vector<MeshContact> out;
    if (!mesh.valid()) return out;
    std::vector<uint32_t> candidates;
    meshQuery(mesh, center - radius, center + radius, candidates);
    for (uint32_t tri : candidates) {
        const Triangle t = triangleAt(mesh, tri);
        const glm::vec3 cp = closestOnTriangle(center, t);
        const glm::vec3 away = center - cp;
        const float d = glm::length(away);
        if (d >= radius) continue;
        out.push_back({d > 1.0e-6f ? away / d : triangleNormal(t), radius - d, cp, tri});
    }
    return out;
}

std::shared_ptr<const ConvexHullData> makeConvexHull(const std::vector<glm::vec3>& input) {
    std::vector<glm::vec3> points;
    for (const glm::vec3& p : input) {
        bool duplicate = false;
        for (const glm::vec3& q : points) {
            if (glm::length(p - q) < 1.0e-6f) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) points.push_back(p);
    }
    const size_t n = points.size();
    if (n < 4 || n > static_cast<size_t>(kMaxHullPoints)) return nullptr;
    glm::vec3 lo(1.0e30f), hi(-1.0e30f);
    for (const glm::vec3& p : points) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const float eps = std::max(1.0e-6f, 1.0e-5f * glm::length(hi - lo));
    auto hull = std::make_shared<ConvexHullData>();
    hull->points = points;
    glm::vec3 centroid(0.0f);
    for (const glm::vec3& p : points) centroid += p;
    centroid /= static_cast<float>(n);
    bool allCoplanar = false;
    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            for (size_t k = j + 1; k < n; ++k) {
                glm::vec3 normal = glm::cross(points[j] - points[i], points[k] - points[i]);
                const float length = glm::length(normal);
                if (length < 1.0e-9f) continue;
                normal /= length;
                int positive = 0, negative = 0;
                for (size_t m = 0; m < n; ++m) {
                    const float s = glm::dot(normal, points[m] - points[i]);
                    if (s > eps) ++positive;
                    else if (s < -eps) ++negative;
                }
                if (positive > 0 && negative > 0) continue;
                if (positive == 0 && negative == 0) allCoplanar = true;
                if (positive > 0) normal = -normal;
                const float d = glm::dot(normal, points[i]);
                bool known = false;
                for (const ConvexHullData::Face& face : hull->faces) {
                    if (glm::dot(face.normal, normal) > 0.9999f && std::abs(face.d - d) < eps * 4.0f) {
                        known = true;
                        break;
                    }
                }
                if (known) continue;
                ConvexHullData::Face face;
                face.normal = normal;
                face.d = d;
                for (size_t m = 0; m < n; ++m) {
                    if (std::abs(glm::dot(normal, points[m]) - d) <= eps) face.indices.push_back(static_cast<uint32_t>(m));
                }
                if (face.indices.size() < 3) continue;
                glm::vec3 fc(0.0f);
                for (uint32_t index : face.indices) fc += points[index];
                fc /= static_cast<float>(face.indices.size());
                const glm::vec3 u = glm::normalize(points[face.indices[0]] - fc);
                const glm::vec3 w = glm::cross(normal, u);
                std::sort(face.indices.begin(), face.indices.end(), [&](uint32_t l, uint32_t r) {
                    const glm::vec3 pl = points[l] - fc;
                    const glm::vec3 pr = points[r] - fc;
                    return std::atan2(glm::dot(pl, w), glm::dot(pl, u)) < std::atan2(glm::dot(pr, w), glm::dot(pr, u));
                });
                hull->faces.push_back(std::move(face));
            }
        }
    }
    if (allCoplanar || hull->faces.size() < 4) return nullptr;
    for (const ConvexHullData::Face& face : hull->faces) {
        for (size_t i = 0; i < face.indices.size(); ++i) {
            uint32_t a = face.indices[i];
            uint32_t b = face.indices[(i + 1) % face.indices.size()];
            if (a > b) std::swap(a, b);
            bool known = false;
            for (const auto& edge : hull->edges) {
                if (edge.first == a && edge.second == b) {
                    known = true;
                    break;
                }
            }
            if (!known) hull->edges.emplace_back(a, b);
        }
    }
    return hull;
}
