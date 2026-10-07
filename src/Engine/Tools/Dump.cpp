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

Json vec3(const glm::vec3& v) {
    Json a = Json::array();
    a.push(v.x);
    a.push(v.y);
    a.push(v.z);
    return a;
}

}  // namespace

std::string hashHex(uint64_t hash) {
    char buf[20];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(hash));
    return buf;
}

uint64_t hashText(std::string_view text) { return hashName(text); }

Json dumpRandom(const RandomService& random) {
    Json out = Json::object();
    out.set("seed", hashHex(random.seed()));
    Json streams = Json::object();
    for (const auto& [name, rng] : random.streams()) streams.set(name, hashHex(rng.state()));
    out.set("streams", std::move(streams));
    return out;
}

Json dumpWorld(const World& world, const DumpOptions& options) {
    Json out = Json::object();
    const TimeState& t = world.time();
    Json time = Json::object();
    time.set("frame", t.frame);
    time.set("elapsed", t.elapsed);
    time.set("scale", t.timeScale);
    time.set("paused", t.paused);
    out.set("time", std::move(time));
    out.set("count", static_cast<uint64_t>(world.aliveCount()));
    Json entities = Json::array();
    world.forEach([&](EntityHandle e) {
        const EntityDescription d = world.describeEntity(e);
        Json j = Json::object();
        j.set("index", e.index);
        j.set("generation", e.generation);
        j.set("name", d.name);
        j.set("tag", d.tag);
        j.set("layer", d.layer);
        j.set("visible", d.visible);
        j.set("parent", d.parent.isNull() ? -1 : static_cast<int>(d.parent.index));
        j.set("children", static_cast<uint64_t>(d.childCount));
        j.set("mesh", d.mesh == NO_MESH ? -1 : static_cast<int64_t>(d.mesh));
        j.set("position", vec3(world.getPosition(e)));
        const glm::quat q = world.getRotation(e);
        Json rot = Json::array();
        rot.push(q.x);
        rot.push(q.y);
        rot.push(q.z);
        rot.push(q.w);
        j.set("rotation", std::move(rot));
        j.set("scale", vec3(world.getScale(e)));
        if (options.worldTransforms) j.set("worldPosition", vec3(world.getWorldPosition(e)));
        if (options.components) {
            Json comps = Json::object();
            for (const auto& [name, text] : d.components) comps.set(name, text);
            j.set("components", std::move(comps));
        }
        if (options.scripts) {
            Json scripts = Json::object();
            for (const auto& [name, text] : d.scripts) scripts.set(name, text);
            j.set("scripts", std::move(scripts));
        }
        entities.push(std::move(j));
    });
    out.set("entities", std::move(entities));
    return out;
}
