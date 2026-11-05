module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

module Engine.Geometry;

import Engine.Physics;

namespace {
constexpr float kParallelEpsilon = 1.0e-12f;
constexpr float kMinDirectionComponent = 1.0e-20f;
constexpr float kOcclusionMinLength = 1.0e-6f;
constexpr float kOcclusionBias = 1.0e-3f;
constexpr uint32_t kLeafTriangleLimit = 4;
constexpr uint32_t kNoTriangle = 0xFFFFFFFFu;
constexpr int kTraversalStackSize = 64;
constexpr int kTraversalStackHeadroom = 62;
constexpr float kPlaneHelperThreshold = 0.9f;
constexpr int kBoxCornerCount = 8;

bool rayTriangle(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& vertexA, const glm::vec3& vertexB, const glm::vec3& vertexC, float& distance) {
    const glm::vec3 edgeAB = vertexB - vertexA;
    const glm::vec3 edgeAC = vertexC - vertexA;
    const glm::vec3 pivot = glm::cross(direction, edgeAC);
    const float determinant = glm::dot(edgeAB, pivot);
    if (std::fabs(determinant) < kParallelEpsilon) return false;
    const float inverseDeterminant = 1.0f / determinant;
    const glm::vec3 toOrigin = origin - vertexA;
    const float baryU = glm::dot(toOrigin, pivot) * inverseDeterminant;
    if (baryU < 0.0f || baryU > 1.0f) return false;
    const glm::vec3 cross = glm::cross(toOrigin, edgeAB);
    const float baryV = glm::dot(direction, cross) * inverseDeterminant;
    if (baryV < 0.0f || baryU + baryV > 1.0f) return false;
    distance = glm::dot(edgeAC, cross) * inverseDeterminant;
    return distance >= 0.0f;
}

bool rayBox(const glm::vec3& origin, const glm::vec3& inverseDirection, const Aabb& box, float maxDistance) {
    float nearDistance = 0.0f;
    float farDistance = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        float slabNear = (box.lo[axis] - origin[axis]) * inverseDirection[axis];
        float slabFar = (box.hi[axis] - origin[axis]) * inverseDirection[axis];
        if (slabNear > slabFar) std::swap(slabNear, slabFar);
        nearDistance = std::max(nearDistance, slabNear);
        farDistance = std::min(farDistance, slabFar);
        if (nearDistance > farDistance) return false;
    }
    return true;
}

float safeInverse(float component) {
    const float magnitude = std::fabs(component) > kMinDirectionComponent ? component : std::copysign(kMinDirectionComponent, component);
    return 1.0f / magnitude;
}

Aabb triangleBox(const TriangleSoup& soup, uint32_t triangle) {
    Aabb box;
    for (uint32_t corner = 0; corner < 3; ++corner) box.grow(soup.vertices[soup.indices[triangle * 3 + corner]]);
    return box;
}

bool usableTriangle(const TriangleSoup& soup, uint32_t triangle) {
    for (uint32_t corner = 0; corner < 3; ++corner) {
        const uint32_t index = soup.indices[triangle * 3 + corner];
        if (index >= soup.vertices.size()) return false;
        const glm::vec3& vertex = soup.vertices[index];
        if (!std::isfinite(vertex.x + vertex.y + vertex.z)) return false;
    }
    return true;
}

const glm::vec3& triangleVertex(const TriangleSoup& soup, uint32_t triangle, uint32_t corner) { return soup.vertices[soup.indices[triangle * 3 + corner]]; }

glm::vec3 triangleNormal(const TriangleSoup& soup, uint32_t triangle, const glm::vec3& rayDirection) {
    const glm::vec3& vertexA = triangleVertex(soup, triangle, 0);
    const glm::vec3 normal = glm::cross(triangleVertex(soup, triangle, 1) - vertexA, triangleVertex(soup, triangle, 2) - vertexA);
    const float length = glm::length(normal);
    const glm::vec3 unit = length > 0.0f ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
    return glm::dot(unit, rayDirection) > 0.0f ? -unit : unit;
}
}

void Aabb::grow(const glm::vec3& point) {
    lo = glm::min(lo, point);
    hi = glm::max(hi, point);
}

void Aabb::grow(const Aabb& other) {
    if (!other.valid()) return;
    lo = glm::min(lo, other.lo);
    hi = glm::max(hi, other.hi);
}

bool Aabb::overlaps(const Aabb& other) const {
    return lo.x <= other.hi.x && hi.x >= other.lo.x && lo.y <= other.hi.y && hi.y >= other.lo.y && lo.z <= other.hi.z && hi.z >= other.lo.z;
}

bool Aabb::contains(const glm::vec3& point) const {
    return point.x >= lo.x && point.x <= hi.x && point.y >= lo.y && point.y <= hi.y && point.z >= lo.z && point.z <= hi.z;
}

void TriangleSoup::addTriangle(const glm::vec3& vertexA, const glm::vec3& vertexB, const glm::vec3& vertexC) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back(vertexA);
    vertices.push_back(vertexB);
    vertices.push_back(vertexC);
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void TriangleSoup::addQuad(const glm::vec3& vertexA, const glm::vec3& vertexB, const glm::vec3& vertexC, const glm::vec3& vertexD) {
    addTriangle(vertexA, vertexB, vertexC);
    addTriangle(vertexA, vertexC, vertexD);
}

void TriangleSoup::addBox(const glm::mat4& transform, const glm::vec3& halfExtents) {
    glm::vec3 corners[kBoxCornerCount];
    for (int cornerIndex = 0; cornerIndex < kBoxCornerCount; ++cornerIndex) {
        const glm::vec4 local((cornerIndex & 1) ? halfExtents.x : -halfExtents.x, (cornerIndex & 2) ? halfExtents.y : -halfExtents.y, (cornerIndex & 4) ? halfExtents.z : -halfExtents.z, 1.0f);
        corners[cornerIndex] = glm::vec3(transform * local);
    }
    vertices.reserve(vertices.size() + 36);
    indices.reserve(indices.size() + 36);
    addQuad(corners[4], corners[5], corners[7], corners[6]);
    addQuad(corners[0], corners[2], corners[3], corners[1]);
    addQuad(corners[2], corners[6], corners[7], corners[3]);
    addQuad(corners[0], corners[1], corners[5], corners[4]);
    addQuad(corners[1], corners[3], corners[7], corners[5]);
    addQuad(corners[0], corners[4], corners[6], corners[2]);
}

void TriangleSoup::addAabb(const Aabb& box) { addBox(glm::translate(glm::mat4(1.0f), box.center()), box.extent()); }

void TriangleSoup::append(const TriangleSoup& other) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    indices.reserve(indices.size() + other.indices.size());
    for (const uint32_t index : other.indices) indices.push_back(base + index);
}

Aabb TriangleSoup::bounds() const {
    Aabb box;
    for (const glm::vec3& vertex : vertices) box.grow(vertex);
    return box;
}

void Bvh::build(const TriangleSoup& soup) {
    m_soup = soup;
    m_nodes.clear();
    const uint32_t triangleTotal = static_cast<uint32_t>(soup.triangleCount());
    m_order.clear();
    m_order.reserve(triangleTotal);
    std::vector<glm::vec3> centroids(triangleTotal);
    for (uint32_t triangle = 0; triangle < triangleTotal; ++triangle) {
        if (!usableTriangle(m_soup, triangle)) continue;
        m_order.push_back(triangle);
        centroids[triangle] = triangleBox(m_soup, triangle).center();
    }
    if (m_order.empty()) return;
    m_nodes.reserve(m_order.size() * 2);
    buildRange(m_order, centroids, 0, static_cast<uint32_t>(m_order.size()));
}

uint32_t Bvh::buildRange(std::vector<uint32_t>& order, std::vector<glm::vec3>& centroids, uint32_t begin, uint32_t end) {
    const uint32_t nodeIndex = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back();
    Aabb box;
    Aabb centroidBox;
    for (uint32_t position = begin; position < end; ++position) {
        box.grow(triangleBox(m_soup, order[position]));
        centroidBox.grow(centroids[order[position]]);
    }
    m_nodes[nodeIndex].box = box;
    if (end - begin <= kLeafTriangleLimit) {
        m_nodes[nodeIndex].first = begin;
        m_nodes[nodeIndex].count = end - begin;
        return nodeIndex;
    }
    const glm::vec3 centroidExtent = centroidBox.hi - centroidBox.lo;
    int axis = 0;
    if (centroidExtent.y > centroidExtent.x) axis = 1;
    if (centroidExtent.z > centroidExtent[axis]) axis = 2;
    const uint32_t middle = begin + (end - begin) / 2;
    std::nth_element(order.begin() + begin, order.begin() + middle, order.begin() + end, [&](uint32_t lhs, uint32_t rhs) {
        const float lhsCoordinate = centroids[lhs][axis];
        const float rhsCoordinate = centroids[rhs][axis];
        return lhsCoordinate != rhsCoordinate ? lhsCoordinate < rhsCoordinate : lhs < rhs;
    });
    buildRange(order, centroids, begin, middle);
    const uint32_t rightChild = buildRange(order, centroids, middle, end);
    m_nodes[nodeIndex].left = nodeIndex + 1;
    m_nodes[nodeIndex].first = rightChild;
    m_nodes[nodeIndex].count = 0;
    return nodeIndex;
}

std::optional<TriHit> Bvh::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    if (m_nodes.empty()) return std::nullopt;
    const glm::vec3 inverseDirection(safeInverse(direction.x), safeInverse(direction.y), safeInverse(direction.z));
    float bestDistance = maxDistance;
    uint32_t bestTriangle = kNoTriangle;
    uint32_t stack[kTraversalStackSize];
    int stackSize = 0;
    stack[stackSize++] = 0;
    while (stackSize > 0) {
        const Node& node = m_nodes[stack[--stackSize]];
        if (!rayBox(origin, inverseDirection, node.box, bestDistance)) continue;
        if (node.count == 0) {
            if (stackSize < kTraversalStackHeadroom) {
                stack[stackSize++] = node.first;
                stack[stackSize++] = node.left;
            }
            continue;
        }
        for (uint32_t slot = 0; slot < node.count; ++slot) {
            const uint32_t triangle = m_order[node.first + slot];
            float distance;
            if (rayTriangle(origin, direction, triangleVertex(m_soup, triangle, 0), triangleVertex(m_soup, triangle, 1), triangleVertex(m_soup, triangle, 2), distance) && distance < bestDistance) {
                bestDistance = distance;
                bestTriangle = triangle;
            }
        }
    }
    if (bestTriangle == kNoTriangle) return std::nullopt;
    TriHit hit;
    hit.t = bestDistance;
    hit.triangle = bestTriangle;
    hit.point = origin + direction * bestDistance;
    hit.normal = triangleNormal(m_soup, bestTriangle, direction);
    return hit;
}

bool Bvh::occluded(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 offset = to - from;
    const float length = glm::length(offset);
    if (length < kOcclusionMinLength) return false;
    return raycast(from, offset / length, length - kOcclusionBias).has_value();
}

void Bvh::query(const Aabb& box, std::vector<uint32_t>& triangles) const {
    if (m_nodes.empty()) return;
    uint32_t stack[kTraversalStackSize];
    int stackSize = 0;
    stack[stackSize++] = 0;
    while (stackSize > 0) {
        const Node& node = m_nodes[stack[--stackSize]];
        if (!node.box.overlaps(box)) continue;
        if (node.count == 0) {
            if (stackSize < kTraversalStackHeadroom) {
                stack[stackSize++] = node.first;
                stack[stackSize++] = node.left;
            }
            continue;
        }
        for (uint32_t slot = 0; slot < node.count; ++slot) {
            const uint32_t triangle = m_order[node.first + slot];
            if (triangleBox(m_soup, triangle).overlaps(box)) triangles.push_back(triangle);
        }
    }
    std::sort(triangles.begin(), triangles.end());
}

void collectColliderGeometry(World& world, TriangleSoup& out, uint32_t layerMask, float planeExtent) {
    world.view<BoxCollider>([&](EntityHandle entity, BoxCollider& box) {
        if (!(layerMask & world.getLayer(entity))) return;
        out.addBox(world.getWorldMatrix(entity), box.halfExtents);
    });
    world.view<PlaneCollider>([&](EntityHandle entity, PlaneCollider& plane) {
        if (!(layerMask & world.getLayer(entity))) return;
        const glm::vec3 normal = glm::normalize(plane.normal);
        const glm::vec3 helper = std::fabs(normal.y) < kPlaneHelperThreshold ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 tangent = glm::normalize(glm::cross(helper, normal)) * planeExtent;
        const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent)) * planeExtent;
        const glm::vec3 center = world.getWorldPosition(entity);
        out.addQuad(center - tangent - bitangent, center + tangent - bitangent, center + tangent + bitangent, center - tangent + bitangent);
    });
}
