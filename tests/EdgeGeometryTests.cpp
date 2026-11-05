#include "TestSupport.hpp"
#include "EdgeSuites.hpp"
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

import Engine.Geometry;
import Engine.glm;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

TriangleSoup groundQuad(float halfSize) {
    TriangleSoup soup;
    soup.addQuad({-halfSize, 0, -halfSize}, {-halfSize, 0, halfSize}, {halfSize, 0, halfSize}, {halfSize, 0, -halfSize});
    return soup;
}

void testAabbEdges() {
    Aabb empty;
    CHECK(!empty.valid());
    CHECK(!empty.contains(glm::vec3(0.0f)));

    Aabb box;
    box.grow(glm::vec3(1.0f));
    CHECK(box.valid());
    CHECK(box.contains(glm::vec3(1.0f)));
    CHECK(glm::length(box.extent()) == 0.0f);

    Aabb grown = box;
    grown.grow(Aabb{});
    CHECK(grown.lo == box.lo && grown.hi == box.hi);

    Aabb touching;
    touching.grow(glm::vec3(1.0f));
    touching.grow(glm::vec3(2.0f));
    CHECK(box.overlaps(touching));
    Aabb apart;
    apart.grow(glm::vec3(1.5f));
    apart.grow(glm::vec3(2.0f));
    CHECK(!box.overlaps(apart));
}

void testSoupEdges() {
    TriangleSoup soup;
    CHECK(soup.triangleCount() == 0);
    CHECK(!soup.bounds().valid());

    TriangleSoup other = groundQuad(1.0f);
    soup.append(other);
    soup.append(other);
    CHECK(soup.triangleCount() == 4);
    CHECK(soup.indices.back() == 11);

    TriangleSoup box;
    box.addBox(glm::mat4(1.0f), glm::vec3(0.5f));
    CHECK(box.triangleCount() == 12);
    const Aabb bounds = box.bounds();
    CHECK(bounds.lo == glm::vec3(-0.5f) && bounds.hi == glm::vec3(0.5f));
}

void testBvhEmptyAndDegenerate() {
    Bvh empty;
    empty.build(TriangleSoup{});
    CHECK(empty.nodeCount() == 0);
    CHECK(!empty.raycast({0, 1, 0}, {0, -1, 0}).has_value());
    CHECK(!empty.occluded({0, 0, 0}, {1, 0, 0}));
    std::vector<uint32_t> found;
    empty.query(Aabb{}, found);
    CHECK(found.empty());

    TriangleSoup degenerate;
    degenerate.addTriangle({0, 0, 0}, {0, 0, 0}, {0, 0, 0});
    degenerate.addTriangle({0, 0, 0}, {1, 0, 0}, {2, 0, 0});
    Bvh bvh;
    bvh.build(degenerate);
    CHECK(!bvh.raycast({0.5f, 1, 0}, {0, -1, 0}).has_value());
}

void testBvhRays() {
    Bvh bvh;
    bvh.build(groundQuad(5.0f));

    const auto down = bvh.raycast({1, 3, 1}, {0, -1, 0});
    CHECK(down.has_value());
    if (down) {
        CHECK(std::fabs(down->t - 3.0f) < 1.0e-4f);
        CHECK(down->normal.y > 0.99f);
    }
    const auto upFromBelow = bvh.raycast({1, -3, 1}, {0, 1, 0});
    CHECK(upFromBelow.has_value());
    if (upFromBelow) CHECK(upFromBelow->normal.y < -0.99f);

    CHECK(!bvh.raycast({1, 3, 1}, {0, 1, 0}).has_value());
    CHECK(!bvh.raycast({1, 3, 1}, {0, -1, 0}, 2.0f).has_value());
    CHECK(!bvh.raycast({9, 3, 9}, {0, -1, 0}).has_value());
    CHECK(!bvh.raycast({1, 3, 1}, {1, 0, 0}).has_value());
    CHECK(bvh.raycast({1, 3, 1}, {0, -1, 0}, 3.5f).has_value());

    const auto zeroDirection = bvh.raycast({1, 3, 1}, {0, 0, 0});
    CHECK(!zeroDirection.has_value());

    const auto nanOrigin = bvh.raycast({kNaN, 3, 1}, {0, -1, 0});
    CHECK(!nanOrigin.has_value());
}

void testBvhOcclusion() {
    TriangleSoup soup = groundQuad(5.0f);
    Bvh bvh;
    bvh.build(soup);
    CHECK(bvh.occluded({0, 1, 0}, {0, -1, 0}));
    CHECK(!bvh.occluded({0, 1, 0}, {0, 2, 0}));
    CHECK(!bvh.occluded({0, 1, 0}, {0, 1, 0}));
    CHECK(!bvh.occluded({0, 1, 0}, {0, 0.0005f, 0}));
}

void testBvhManyIdenticalTriangles() {
    TriangleSoup soup;
    for (int copy = 0; copy < 500; ++copy) soup.addTriangle({-1, 0, -1}, {-1, 0, 1}, {1, 0, 0});
    Bvh bvh;
    bvh.build(soup);
    CHECK(bvh.triangleCount() == 500);
    CHECK(bvh.nodeCount() > 1);
    CHECK(bvh.raycast({-0.5f, 2, 0}, {0, -1, 0}).has_value());
    std::vector<uint32_t> found;
    Aabb everything;
    everything.grow(glm::vec3(-10.0f));
    everything.grow(glm::vec3(10.0f));
    bvh.query(everything, found);
    CHECK(found.size() == 500);
}

void testBvhQueryBoundary() {
    TriangleSoup soup;
    for (int cell = 0; cell < 64; ++cell) {
        const float x = static_cast<float>(cell) * 2.0f;
        soup.addTriangle({x, 0, 0}, {x + 1, 0, 0}, {x, 0, 1});
    }
    Bvh bvh;
    bvh.build(soup);
    std::vector<uint32_t> found;
    Aabb touching;
    touching.grow(glm::vec3(1.0f, -1.0f, -1.0f));
    touching.grow(glm::vec3(1.0f, 1.0f, 1.0f));
    bvh.query(touching, found);
    CHECK(found.size() == 1 && found[0] == 0);
    found.clear();
    Aabb gap;
    gap.grow(glm::vec3(1.2f, -1.0f, -1.0f));
    gap.grow(glm::vec3(1.8f, 1.0f, 1.0f));
    bvh.query(gap, found);
    CHECK(found.empty());
}

}

void runEdgeGeometryTests() {
    testAabbEdges();
    testSoupEdges();
    testBvhEmptyAndDegenerate();
    testBvhRays();
    testBvhOcclusion();
    testBvhManyIdenticalTriangles();
    testBvhQueryBoundary();
}
