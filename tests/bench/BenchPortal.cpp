#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Harness.hpp"

import Engine.Portal;
import Engine.Portal.view;
import Engine.World;
import Engine.camera;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

#include "WorldSetup.hpp"
#include "PortalLine.hpp"

namespace bench {

namespace {

constexpr int kCellCount = 12;
constexpr size_t kEntitiesPerCell = 2000;
constexpr size_t kQueryCount = 500;
constexpr float kRayLength = 120.0f;
constexpr float kOverlapRadius = 3.0f;
constexpr float kViewportHeight = 1080.0f;
constexpr size_t kStepCount = 1000;

struct PortalFixture {
    PortalSystem portals;
    std::unique_ptr<World> world;
};

std::shared_ptr<PortalFixture> buildFixture() {
    auto fixture = std::make_shared<PortalFixture>();
    buildPortalLine(fixture->portals, kCellCount);
    fixture->world = makeEmptyWorld();
    Pcg32 rng(kSeed + 7);
    fixture->world->createBatch(kEntitiesPerCell * kCellCount, [&](size_t index, EntityDesc& desc) {
        desc.mesh = kBenchMesh;
        const float cellX = kSpacing * static_cast<float>(index / kEntitiesPerCell);
        desc.position = glm::vec3(cellX + (rng.uniform() - kBenchHalf) * 2.0f * (kCellHalfX - 0.5f), rng.uniform() * 4.0f, (rng.uniform() - kBenchHalf) * 2.0f * (kCellHalfX - 0.5f));
    });
    fixture->world->overlapSphere(glm::vec3(0.0f), 1.0f);
    return fixture;
}

Case raycastCase() {
    auto fixture = buildFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kQueryCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        Pcg32 rng(kSeed + 8);
        int crossings = 0;
        for (size_t index = 0; index < kQueryCount; ++index) {
            const glm::vec3 origin(rng.uniform() * 4.0f - 2.0f, 1.5f, rng.uniform() * 2.0f - 1.0f);
            crossings += fixture->portals.raycast(*fixture->world, origin, glm::vec3(1.0f, 0.0f, 0.0f), kRayLength, 0).crossings;
        }
        sink(crossings);
    };
    return benchCase;
}

Case overlapCase() {
    auto fixture = buildFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kQueryCount;
    benchCase.iterations = 20;
    benchCase.run = [fixture] {
        size_t found = 0;
        for (size_t index = 0; index < kQueryCount; ++index) {
            const float x = kSpacing * static_cast<float>(index % (kCellCount - 1)) + kPortalOffset;
            found += fixture->portals.overlapSphere(*fixture->world, glm::vec3(x, 1.5f, 0.0f), kOverlapRadius, static_cast<int>(index % (kCellCount - 1))).size();
        }
        sink(static_cast<double>(found));
    };
    return benchCase;
}

Case frameCase() {
    auto fixture = buildFixture();
    auto camera = std::make_shared<Camera>();
    camera->setPos(glm::vec3(0.0f, 1.7f, 0.0f));
    camera->setOrientation(0.0f, 0.0f);
    camera->updateAspectRatio(16.0f, 9.0f);
    camera->setNearPlane(0.1f);
    camera->setFarPlane(500.0f);
    fixture->portals.setCameraCell(0);
    Case benchCase;
    benchCase.itemsPerIteration = 1.0;
    benchCase.iterations = 200;
    benchCase.run = [fixture, camera] { sink(static_cast<double>(fixture->portals.buildFrame(*camera, kViewportHeight).nodes.size())); };
    return benchCase;
}

Case stepCase() {
    auto fixture = buildFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kStepCount;
    benchCase.iterations = 50;
    benchCase.run = [fixture] {
        glm::vec3 position(0.0f, 1.5f, 0.0f);
        int cell = 0;
        for (size_t index = 0; index < kStepCount; ++index) {
            const glm::vec3 previous = position;
            position.x += 0.1f;
            fixture->portals.stepPoint(position, cell, previous);
            if (cell + 1 >= kCellCount && position.x > kSpacing * kCellCount) position.x = 0.0f;
        }
        sink(position.x);
    };
    return benchCase;
}

}

void registerPortalBenches() {
    add("portal.raycast_12cells_x500", raycastCase);
    add("portal.overlap_sphere_12cells_x500", overlapCase);
    add("portal.build_frame", frameCase);
    add("portal.step_point_x1000", stepCase);
}

}
