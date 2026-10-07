module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

module Engine.Geometry;

import Engine.Physics;

void Aabb::grow(const glm::vec3& p) {
    lo = glm::min(lo, p);
    hi = glm::max(hi, p);
}

void Aabb::grow(const Aabb& o) {
    if (!o.valid()) return;
    lo = glm::min(lo, o.lo);
    hi = glm::max(hi, o.hi);
}

bool Aabb::overlaps(const Aabb& o) const { return lo.x <= o.hi.x && hi.x >= o.lo.x && lo.y <= o.hi.y && hi.y >= o.lo.y && lo.z <= o.hi.z && hi.z >= o.lo.z; }

bool Aabb::contains(const glm::vec3& p) const { return p.x >= lo.x && p.x <= hi.x && p.y >= lo.y && p.y <= hi.y && p.z >= lo.z && p.z <= hi.z; }

void TriangleSoup::addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.push_back(a);
    vertices.push_back(b);
    vertices.push_back(c);
    indices.push_back(base);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void TriangleSoup::addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d) {
    addTriangle(a, b, c);
    addTriangle(a, c, d);
}

void TriangleSoup::addBox(const glm::mat4& transform, const glm::vec3& h) {
    glm::vec3 p[8];
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 w = transform * glm::vec4((i & 1) ? h.x : -h.x, (i & 2) ? h.y : -h.y, (i & 4) ? h.z : -h.z, 1.0f);
        p[i] = glm::vec3(w.x, w.y, w.z);
    }
    addQuad(p[4], p[5], p[7], p[6]);
    addQuad(p[0], p[2], p[3], p[1]);
    addQuad(p[2], p[6], p[7], p[3]);
    addQuad(p[0], p[1], p[5], p[4]);
    addQuad(p[1], p[3], p[7], p[5]);
    addQuad(p[0], p[4], p[6], p[2]);
}

void TriangleSoup::addAabb(const Aabb& box) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), box.center());
    addBox(m, box.extent());
}

void TriangleSoup::append(const TriangleSoup& other) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    vertices.insert(vertices.end(), other.vertices.begin(), other.vertices.end());
    for (const uint32_t i : other.indices) indices.push_back(base + i);
}

Aabb TriangleSoup::bounds() const {
    Aabb b;
    for (const glm::vec3& v : vertices) b.grow(v);
    return b;
}

namespace {
bool rayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t) {
    const glm::vec3 e1 = b - a;
    const glm::vec3 e2 = c - a;
    const glm::vec3 p = glm::cross(d, e2);
    const float det = glm::dot(e1, p);
    if (std::fabs(det) < 1.0e-12f) return false;
    const float inv = 1.0f / det;
    const glm::vec3 s = o - a;
    const float u = glm::dot(s, p) * inv;
    if (u < 0.0f || u > 1.0f) return false;
    const glm::vec3 q = glm::cross(s, e1);
    const float v = glm::dot(d, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = glm::dot(e2, q) * inv;
    return t >= 0.0f;
}

bool rayBox(const glm::vec3& o, const glm::vec3& invDir, const Aabb& box, float maxT) {
    float t0 = 0.0f;
    float t1 = maxT;
    for (int i = 0; i < 3; ++i) {
        float a = (box.lo[i] - o[i]) * invDir[i];
        float b = (box.hi[i] - o[i]) * invDir[i];
        if (a > b) std::swap(a, b);
        t0 = std::max(t0, a);
        t1 = std::min(t1, b);
        if (t0 > t1) return false;
    }
    return true;
}

Aabb triBox(const TriangleSoup& s, uint32_t tri) {
    Aabb b;
    for (int k = 0; k < 3; ++k) b.grow(s.vertices[s.indices[tri * 3 + k]]);
    return b;
}
}

void Bvh::build(const TriangleSoup& soup) {
    m_soup = soup;
    m_nodes.clear();
    const uint32_t n = static_cast<uint32_t>(soup.triangleCount());
    m_order.resize(n);
    std::vector<glm::vec3> centroids(n);
    for (uint32_t i = 0; i < n; ++i) {
        m_order[i] = i;
        centroids[i] = triBox(m_soup, i).center();
    }
    if (n == 0) return;
    m_nodes.reserve(n * 2);
    buildRange(m_order, centroids, 0, n);
}

uint32_t Bvh::buildRange(std::vector<uint32_t>& order, std::vector<glm::vec3>& centroids, uint32_t begin, uint32_t end) {
    const uint32_t index = static_cast<uint32_t>(m_nodes.size());
    m_nodes.emplace_back();
    Aabb box;
    Aabb cbox;
    for (uint32_t i = begin; i < end; ++i) {
        box.grow(triBox(m_soup, order[i]));
        cbox.grow(centroids[order[i]]);
    }
    m_nodes[index].box = box;
    if (end - begin <= 4) {
        m_nodes[index].first = begin;
        m_nodes[index].count = end - begin;
        return index;
    }
    const glm::vec3 ext = cbox.hi - cbox.lo;
    int axis = 0;
    if (ext.y > ext.x) axis = 1;
    if (ext.z > ext[axis]) axis = 2;
    const uint32_t mid = (begin + end) / 2;
    std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end, [&](uint32_t a, uint32_t b) {
        const float ca = centroids[a][axis];
        const float cb = centroids[b][axis];
        return ca != cb ? ca < cb : a < b;
    });
    buildRange(order, centroids, begin, mid);
    const uint32_t right = buildRange(order, centroids, mid, end);
    m_nodes[index].left = index + 1;
    m_nodes[index].first = right;
    m_nodes[index].count = 0;
    return index;
}

std::optional<TriHit> Bvh::raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance) const {
    if (m_nodes.empty()) return std::nullopt;
    const glm::vec3 inv(1.0f / (std::fabs(direction.x) > 1.0e-20f ? direction.x : 1.0e-20f), 1.0f / (std::fabs(direction.y) > 1.0e-20f ? direction.y : 1.0e-20f),
                        1.0f / (std::fabs(direction.z) > 1.0e-20f ? direction.z : 1.0e-20f));
    float best = maxDistance;
    uint32_t bestTri = 0xFFFFFFFFu;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& node = m_nodes[stack[--sp]];
        if (!rayBox(origin, inv, node.box, best)) continue;
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                const uint32_t tri = m_order[node.first + i];
                float t;
                if (rayTriangle(origin, direction, m_soup.vertices[m_soup.indices[tri * 3]], m_soup.vertices[m_soup.indices[tri * 3 + 1]], m_soup.vertices[m_soup.indices[tri * 3 + 2]], t) && t < best) {
                    best = t;
                    bestTri = tri;
                }
            }
        } else if (sp < 62) {
            stack[sp++] = node.first;
            stack[sp++] = node.left;
        }
    }
    if (bestTri == 0xFFFFFFFFu) return std::nullopt;
    TriHit hit;
    hit.t = best;
    hit.triangle = bestTri;
    hit.point = origin + direction * best;
    const glm::vec3& a = m_soup.vertices[m_soup.indices[bestTri * 3]];
    const glm::vec3& b = m_soup.vertices[m_soup.indices[bestTri * 3 + 1]];
    const glm::vec3& c = m_soup.vertices[m_soup.indices[bestTri * 3 + 2]];
    glm::vec3 n = glm::cross(b - a, c - a);
    const float len = glm::length(n);
    n = len > 0.0f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::dot(n, direction) > 0.0f) n = -n;
    hit.normal = n;
    return hit;
}

bool Bvh::occluded(const glm::vec3& from, const glm::vec3& to) const {
    const glm::vec3 d = to - from;
    const float len = glm::length(d);
    if (len < 1.0e-6f) return false;
    return raycast(from, d / len, len - 1.0e-3f).has_value();
}

void Bvh::query(const Aabb& box, std::vector<uint32_t>& out) const {
    if (m_nodes.empty()) return;
    uint32_t stack[64];
    int sp = 0;
    stack[sp++] = 0;
    while (sp > 0) {
        const Node& node = m_nodes[stack[--sp]];
        if (!node.box.overlaps(box)) continue;
        if (node.count > 0) {
            for (uint32_t i = 0; i < node.count; ++i) {
                const uint32_t tri = m_order[node.first + i];
                if (triBox(m_soup, tri).overlaps(box)) out.push_back(tri);
            }
        } else if (sp < 62) {
            stack[sp++] = node.first;
            stack[sp++] = node.left;
        }
    }
    std::sort(out.begin(), out.end());
}

void collectColliderGeometry(World& world, TriangleSoup& out, uint32_t layerMask, float planeExtent) {
    world.view<BoxCollider>([&](EntityHandle e, BoxCollider& box) {
        if (!(layerMask & world.getLayer(e))) return;
        out.addBox(world.getWorldMatrix(e), box.halfExtents);
    });
    world.view<PlaneCollider>([&](EntityHandle e, PlaneCollider& plane) {
        if (!(layerMask & world.getLayer(e))) return;
        const glm::vec3 n = glm::normalize(plane.normal);
        const glm::vec3 helper = std::fabs(n.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 u = glm::normalize(glm::cross(helper, n)) * planeExtent;
        const glm::vec3 v = glm::normalize(glm::cross(n, u)) * planeExtent;
        const glm::vec3 c = world.getWorldPosition(e);
        out.addQuad(c - u - v, c + u - v, c + u + v, c - u + v);
    });
}
