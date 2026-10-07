module;

#include <cstdint>
#include <optional>
#include <vector>

export module Engine.Geometry;

import Engine.glm;
import Engine.World;
import Engine.Render.entity;

export struct Aabb {
    glm::vec3 lo{1.0e30f};
    glm::vec3 hi{-1.0e30f};

    void grow(const glm::vec3& p);
    void grow(const Aabb& o);
    [[nodiscard]] bool valid() const { return lo.x <= hi.x; }
    [[nodiscard]] glm::vec3 center() const { return (lo + hi) * 0.5f; }
    [[nodiscard]] glm::vec3 extent() const { return (hi - lo) * 0.5f; }
    [[nodiscard]] bool overlaps(const Aabb& o) const;
    [[nodiscard]] bool contains(const glm::vec3& p) const;
};

export struct TriangleSoup {
    std::vector<glm::vec3> vertices;
    std::vector<uint32_t> indices;

    void addTriangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c);
    void addQuad(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d);
    void addBox(const glm::mat4& transform, const glm::vec3& halfExtents);
    void addAabb(const Aabb& box);
    void append(const TriangleSoup& other);
    [[nodiscard]] size_t triangleCount() const { return indices.size() / 3; }
    [[nodiscard]] Aabb bounds() const;
};

export struct TriHit {
    float t = 0.0f;
    uint32_t triangle = 0;
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

export class Bvh {
  public:
    void build(const TriangleSoup& soup);
    [[nodiscard]] std::optional<TriHit> raycast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance = 1.0e30f) const;
    [[nodiscard]] bool occluded(const glm::vec3& from, const glm::vec3& to) const;
    void query(const Aabb& box, std::vector<uint32_t>& triangles) const;
    [[nodiscard]] const TriangleSoup& soup() const { return m_soup; }
    [[nodiscard]] Aabb bounds() const { return m_nodes.empty() ? Aabb{} : m_nodes[0].box; }
    [[nodiscard]] size_t nodeCount() const { return m_nodes.size(); }
    [[nodiscard]] size_t triangleCount() const { return m_soup.triangleCount(); }

  private:
    struct Node {
        Aabb box;
        uint32_t left = 0;
        uint32_t first = 0;
        uint32_t count = 0;
    };
    uint32_t buildRange(std::vector<uint32_t>& order, std::vector<glm::vec3>& centroids, uint32_t begin, uint32_t end);

    TriangleSoup m_soup;
    std::vector<Node> m_nodes;
    std::vector<uint32_t> m_order;
};

export void collectColliderGeometry(World& world, TriangleSoup& out, uint32_t layerMask = 0xFFFFFFFFu, float planeExtent = 500.0f);
