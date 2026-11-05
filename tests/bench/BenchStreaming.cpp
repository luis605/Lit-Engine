#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "Harness.hpp"

import Engine.Portal;
import Engine.Streaming;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;

#include "PortalLine.hpp"

namespace bench {

namespace {

constexpr int kCellCount = 8;
constexpr int kEntitiesPerCell = 500;
constexpr int kSettleLimit = 100000;
constexpr size_t kSettledUpdates = 1000;
constexpr double kCommitMillis = 50.0;
constexpr size_t kUploadBytes = 64u << 20;
constexpr float kRowSpacing = 0.5f;
constexpr float kEntityY = 0.5f;
constexpr int kRowLength = 10;

struct StreamFixture {
    PortalSystem portals;
    World world;
    std::unique_ptr<StreamingSystem> streaming;
    int cell = 0;
    int direction = 1;
};

CellContent buildContent(int cell) {
    CellContent content;
    for (int index = 0; index < kEntitiesPerCell; ++index) {
        CellEntity entity;
        entity.key = "k" + std::to_string(index);
        entity.desc.name = "c" + std::to_string(cell) + "_" + std::to_string(index);
        entity.desc.position = glm::vec3(kSpacing * static_cast<float>(cell) + static_cast<float>(index % kRowLength) * kRowSpacing, kEntityY, static_cast<float>(index / kRowLength) * kRowSpacing);
        content.entities.push_back(std::move(entity));
    }
    return content;
}

std::shared_ptr<StreamFixture> buildFixture() {
    auto fixture = std::make_shared<StreamFixture>();
    buildPortalLine(fixture->portals, kCellCount);
    StreamingConfig config;
    config.commitMillisPerFrame = kCommitMillis;
    config.uploadBytesPerFrame = kUploadBytes;
    fixture->streaming = std::make_unique<StreamingSystem>(fixture->portals, config);
    fixture->streaming->setLoader(makeBatchLoader(fixture->world, fixture->portals, nullptr, buildContent));
    fixture->streaming->prime(0);
    return fixture;
}

void settle(StreamFixture& fixture) {
    for (int attempt = 0; attempt < kSettleLimit && !fixture.streaming->settled(); ++attempt) {
        fixture.streaming->update(fixture.cell);
        std::this_thread::yield();
    }
}

Case settledCase() {
    auto fixture = buildFixture();
    settle(*fixture);
    Case benchCase;
    benchCase.itemsPerIteration = kSettledUpdates;
    benchCase.iterations = 30;
    benchCase.run = [fixture] {
        for (size_t index = 0; index < kSettledUpdates; ++index) fixture->streaming->update(fixture->cell);
    };
    return benchCase;
}

Case transitionCase() {
    auto fixture = buildFixture();
    settle(*fixture);
    Case benchCase;
    benchCase.itemsPerIteration = kEntitiesPerCell;
    benchCase.iterations = 20;
    benchCase.warmup = 2;
    benchCase.prepare = [fixture] {
        if (fixture->cell + fixture->direction < 0 || fixture->cell + fixture->direction >= kCellCount) fixture->direction = -fixture->direction;
        fixture->cell += fixture->direction;
    };
    benchCase.run = [fixture] {
        fixture->streaming->update(fixture->cell);
        settle(*fixture);
    };
    return benchCase;
}

}

void registerStreamingBenches() {
    add("streaming.update_settled_x1000", settledCase);
    add("streaming.cell_transition_500ent", transitionCase);
}

}
