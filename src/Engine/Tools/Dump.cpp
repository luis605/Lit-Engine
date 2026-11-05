module;

#include <cstdint>
#include <functional>
#include <typeinfo>
#include <cstdio>
#include <string>
#include <string_view>

module Engine.Dump;

import Engine.glm;
import Engine.Render.entity;
import Engine.Render.component;

namespace {
constexpr size_t kHashBufferSize = 20;

Json vec3Json(const glm::vec3& vector) {
    Json array = Json::array();
    array.push(vector.x);
    array.push(vector.y);
    array.push(vector.z);
    return array;
}

Json quatJson(const glm::quat& rotation) {
    Json array = Json::array();
    array.push(rotation.x);
    array.push(rotation.y);
    array.push(rotation.z);
    array.push(rotation.w);
    return array;
}

Json textMapJson(const auto& entries) {
    Json object = Json::object();
    for (const auto& [name, text] : entries) object.set(name, text);
    return object;
}

Json timeJson(const TimeState& state) {
    Json time = Json::object();
    time.set("frame", state.frame);
    time.set("elapsed", state.elapsed);
    time.set("scale", state.timeScale);
    time.set("paused", state.paused);
    return time;
}

Json dumpEntity(const World& world, EntityHandle entity, const DumpOptions& options) {
    const EntityDescription description = world.describeEntity(entity);
    Json json = Json::object();
    json.set("index", entity.index);
    json.set("generation", entity.generation);
    json.set("name", description.name);
    json.set("tag", description.tag);
    json.set("layer", description.layer);
    json.set("visible", description.visible);
    json.set("parent", description.parent.isNull() ? -1 : static_cast<int>(description.parent.index));
    json.set("children", static_cast<uint64_t>(description.childCount));
    json.set("mesh", description.mesh == NO_MESH ? -1 : static_cast<int64_t>(description.mesh));
    json.set("position", vec3Json(world.getPosition(entity)));
    json.set("rotation", quatJson(world.getRotation(entity)));
    json.set("scale", vec3Json(world.getScale(entity)));
    if (options.worldTransforms) json.set("worldPosition", vec3Json(world.getWorldPosition(entity)));
    if (options.components) json.set("components", textMapJson(description.components));
    if (options.scripts) json.set("scripts", textMapJson(description.scripts));
    return json;
}
}

std::string hashHex(uint64_t hash) {
    char buffer[kHashBufferSize];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

uint64_t hashText(std::string_view text) { return hashName(text); }

Json dumpRandom(const RandomService& random) {
    Json out = Json::object();
    out.set("seed", hashHex(random.seed()));
    Json streams = Json::object();
    for (const auto& [name, stream] : random.streams()) streams.set(name, hashHex(stream.state()));
    out.set("streams", std::move(streams));
    return out;
}

Json dumpWorld(const World& world, const DumpOptions& options) {
    Json out = Json::object();
    out.set("time", timeJson(world.time()));
    out.set("count", static_cast<uint64_t>(world.aliveCount()));
    Json entities = Json::array();
    world.forEach([&](EntityHandle entity) { entities.push(dumpEntity(world, entity, options)); });
    out.set("entities", std::move(entities));
    return out;
}
