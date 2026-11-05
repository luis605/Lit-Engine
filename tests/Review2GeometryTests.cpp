#include "Review2Suites.hpp"
#include "TestSupport.hpp"
#include <cmath>
#include <limits>
#include <vector>

import Engine.Geometry;
import Engine.glm;

namespace {

constexpr float kQuiet = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

void testBadIndicesAreIgnored() {
    TriangleSoup soup;
    soup.addTriangle({-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    soup.indices.push_back(0);
    soup.indices.push_back(1);
    soup.indices.push_back(9999);
    soup.indices.push_back(2);
    soup.indices.push_back(1);
    Bvh bvh;
    bvh.build(soup);
    const auto hit = bvh.raycast({0.0f, 5.0f, 0.0f}, {0.0f, -1.0f, 0.0f});
    CHECK(hit.has_value() && hit->triangle == 0);
    Aabb everything;
    everything.grow(glm::vec3(-100.0f));
    everything.grow(glm::vec3(100.0f));
    std::vector<uint32_t> found;
    bvh.query(everything, found);
    CHECK(found.size() == 1);
}

void testNonFiniteVertices() {
    TriangleSoup soup;
    soup.addTriangle({kQuiet, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    soup.addTriangle({kInf, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f});
    Bvh bvh;
    bvh.build(soup);
    CHECK(bvh.nodeCount() == 0);
    CHECK(!bvh.raycast({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}).has_value());
    for (int copies = 0; copies < 40; ++copies) soup.addTriangle({-1.0f, 0.0f, static_cast<float>(copies)}, {1.0f, 0.0f, static_cast<float>(copies)}, {0.0f, 0.0f, static_cast<float>(copies) + 1.0f});
    bvh.build(soup);
    CHECK(bvh.bounds().valid());
    CHECK(std::isfinite(bvh.bounds().hi.x + bvh.bounds().lo.z));
    CHECK(bvh.raycast({0.0f, 1.0f, 5.2f}, {0.0f, -1.0f, 0.0f}).has_value());
}

void testDegenerateInput() {
    TriangleSoup soup;
    for (int copies = 0; copies < 5000; ++copies) soup.addTriangle({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    Bvh bvh;
    bvh.build(soup);
    CHECK(bvh.triangleCount() == 5000);
    CHECK(!bvh.raycast({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}).has_value());
    CHECK(!bvh.raycast({kQuiet, kQuiet, kQuiet}, {kQuiet, 0.0f, 0.0f}).has_value());
    CHECK(!bvh.raycast({0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}).has_value());
    CHECK(!bvh.occluded({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}));
    CHECK(!bvh.occluded({kQuiet, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}));
    std::vector<uint32_t> found;
    bvh.query(Aabb{}, found);
    CHECK(found.empty());
    Bvh empty;
    empty.build(TriangleSoup{});
    CHECK(empty.nodeCount() == 0 && !empty.bounds().valid());

    TriangleSoup stacked;
    for (int copies = 0; copies < 3000; ++copies) stacked.addTriangle({-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f});
    Bvh stackedBvh;
    stackedBvh.build(stacked);
    CHECK(stackedBvh.raycast({0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f}).has_value());
}

}

void runReview2GeometryTests() {
    testBadIndicesAreIgnored();
    testNonFiniteVertices();
    testDegenerateInput();
}
