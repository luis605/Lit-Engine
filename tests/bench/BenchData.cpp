#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Harness.hpp"

import Engine.Json;
import Engine.Delta;
import Engine.Save;
import Engine.World;
import Engine.Render.entity;
import Engine.glm;
import Engine.Rng;

#include "WorldSetup.hpp"

namespace bench {

namespace {

constexpr size_t kJsonEntities = 50000;
constexpr size_t kSaveEntities = 20000;
constexpr size_t kDeltaEntities = 20000;
constexpr size_t kDeltaChanged = 2000;
constexpr float kMoveStep = 0.25f;

Json vectorJson(float first, float second, float third) {
    Json array = Json::array();
    array.push(first);
    array.push(second);
    array.push(third);
    return array;
}

Json buildSceneJson() {
    Pcg32 rng(kSeed + 9);
    Json entities = Json::array();
    for (size_t index = 0; index < kJsonEntities; ++index) {
        Json entity = Json::object();
        entity.set("name", "entity_" + std::to_string(index));
        entity.set("mesh", static_cast<int>(index % 16));
        entity.set("visible", index % 7 != 0);
        entity.set("position", vectorJson(rng.uniform() * 400.0f, rng.uniform() * 40.0f, rng.uniform() * 400.0f));
        entity.set("scale", vectorJson(1.0f, 1.0f, 1.0f));
        Json components = Json::object();
        components.set("Health", static_cast<int>(rng.below(100)));
        components.set("Tint", "0.5 0.25 0.75");
        entity.set("components", std::move(components));
        entities.push(std::move(entity));
    }
    Json scene = Json::object();
    scene.set("version", 3);
    scene.set("entities", std::move(entities));
    return scene;
}

Case jsonDumpCase() {
    auto scene = std::make_shared<Json>(buildSceneJson());
    Case benchCase;
    benchCase.itemsPerIteration = kJsonEntities;
    benchCase.iterations = 10;
    benchCase.run = [scene] { sink(static_cast<double>(scene->dump().size())); };
    return benchCase;
}

Case jsonParseCase() {
    auto text = std::make_shared<std::string>(buildSceneJson().dump());
    Case benchCase;
    benchCase.itemsPerIteration = kJsonEntities;
    benchCase.iterations = 10;
    benchCase.run = [text] {
        const auto parsed = Json::parse(*text);
        sink(parsed ? static_cast<double>(parsed->size()) : 0.0);
    };
    return benchCase;
}

Case saveCaptureCase() {
    auto world = std::shared_ptr<World>(makeScatteredWorld(kSaveEntities));
    auto save = std::make_shared<SaveSystem>(SaveContext{world.get()});
    Case benchCase;
    benchCase.itemsPerIteration = kSaveEntities;
    benchCase.iterations = 10;
    benchCase.run = [world, save] { sink(static_cast<double>(save->capture().size())); };
    return benchCase;
}

Case saveApplyCase() {
    auto world = std::shared_ptr<World>(makeScatteredWorld(kSaveEntities));
    auto save = std::make_shared<SaveSystem>(SaveContext{world.get()});
    auto document = std::make_shared<Json>(save->capture());
    Case benchCase;
    benchCase.itemsPerIteration = kSaveEntities;
    benchCase.iterations = 10;
    benchCase.run = [world, save, document] { sink(save->apply(*document) ? 1.0 : 0.0); };
    return benchCase;
}

struct DeltaFixture {
    std::unique_ptr<World> world;
    std::unique_ptr<DeltaStore> store;
    std::vector<EntityHandle> handles;
};

std::shared_ptr<DeltaFixture> buildDeltaFixture() {
    auto fixture = std::make_shared<DeltaFixture>();
    fixture->world = makeScatteredWorld(kDeltaEntities);
    fixture->store = std::make_unique<DeltaStore>(*fixture->world);
    fixture->handles = allHandles(*fixture->world);
    for (size_t index = 0; index < fixture->handles.size(); ++index) fixture->store->registerBase("A", "k" + std::to_string(index), fixture->handles[index]);
    fixture->store->applyCell("A");
    return fixture;
}

Case deltaCaptureApplyCase() {
    auto fixture = buildDeltaFixture();
    Case benchCase;
    benchCase.itemsPerIteration = kDeltaChanged;
    benchCase.iterations = 10;
    benchCase.run = [fixture] {
        for (size_t index = 0; index < kDeltaChanged; ++index) fixture->world->translate(fixture->handles[index], glm::vec3(kMoveStep, 0.0f, 0.0f));
        fixture->store->captureCell("A");
        fixture->store->applyCell("A");
    };
    return benchCase;
}

Case deltaJsonCase() {
    auto fixture = buildDeltaFixture();
    for (size_t index = 0; index < kDeltaChanged; ++index) fixture->world->translate(fixture->handles[index], glm::vec3(kMoveStep, 0.0f, 0.0f));
    Case benchCase;
    benchCase.itemsPerIteration = kDeltaChanged;
    benchCase.iterations = 10;
    benchCase.run = [fixture] {
        const Json document = fixture->store->toJson();
        sink(fixture->store->fromJson(document) ? 1.0 : 0.0);
    };
    return benchCase;
}

}

void registerDataBenches() {
    add("data.json_dump_50k_entities", jsonDumpCase);
    add("data.json_parse_50k_entities", jsonParseCase);
    add("data.save_capture_20k", saveCaptureCase);
    add("data.save_apply_20k", saveApplyCase);
    add("data.delta_capture_apply_2k_of_20k", deltaCaptureApplyCase);
    add("data.delta_json_roundtrip_2k_of_20k", deltaJsonCase);
}

}
