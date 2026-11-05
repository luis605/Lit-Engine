module;

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <typeinfo>
#include <string>
#include <string_view>
#include <vector>

module Engine.WorldHash;

import Engine.glm;
import Engine.Json;
import Engine.Rng;
import Engine.Render.entity;

namespace {
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr int64_t kNonFiniteSentinel = INT64_MIN;
constexpr double kQuantiseLimit = 1.0e14;
constexpr uint64_t kSectionWorld = 1;
constexpr uint64_t kSectionRandom = 2;
constexpr uint64_t kSectionState = 3;
constexpr uint64_t kSectionStorylets = 4;
constexpr uint64_t kSectionParams = 5;
constexpr uint64_t kSectionNav = 6;
constexpr uint64_t kSectionExtra = 7;

void hashEntity(StateHasher& hasher, const World& world, EntityHandle entity) {
    const EntityDescription description = world.describeEntity(entity);
    hasher.addU64(entity.index);
    hasher.addU64(entity.generation);
    hasher.addText(description.name);
    hasher.addText(description.tag);
    hasher.addU64(description.layer);
    hasher.addU64(description.mesh);
    hasher.addBool(description.visible);
    hasher.addU64(description.parent.isNull() ? ~0ull : description.parent.index);
    hasher.addU64(description.childCount);
    const glm::vec3 position = world.getPosition(entity);
    const glm::vec3 scale = world.getScale(entity);
    glm::quat rotation = world.getRotation(entity);
    if (rotation.w < 0.0f) rotation = glm::quat(-rotation.w, -rotation.x, -rotation.y, -rotation.z);
    hasher.addVec3(position.x, position.y, position.z);
    hasher.addVec3(scale.x, scale.y, scale.z);
    hasher.addFloat(rotation.x);
    hasher.addFloat(rotation.y);
    hasher.addFloat(rotation.z);
    hasher.addFloat(rotation.w);
    for (const auto& [name, text] : description.components) {
        hasher.addText(name);
        hasher.addText(text);
    }
    for (const auto& [name, text] : description.scripts) {
        hasher.addText(name);
        hasher.addText(text);
    }
}

void hashRandom(StateHasher& hasher, const RandomService& random) {
    hasher.addU64(random.seed());
    for (const auto& [name, stream] : random.streams()) {
        hasher.addText(name);
        hasher.addU64(stream.state());
        hasher.addU64(stream.increment());
    }
}

void hashParams(StateHasher& hasher, const ParamBus& params) {
    for (const ParamInfo& info : params.params()) {
        hasher.addText(info.name);
        hasher.addU64(info.slot);
        hasher.addFloat(info.target);
        hasher.addFloat(info.value);
    }
}
}

void StateHasher::addBytes(const void* data, size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t index = 0; index < size; ++index) m_state = (m_state ^ bytes[index]) * kFnvPrime;
}

void StateHasher::addU64(uint64_t value) {
    unsigned char bytes[sizeof value];
    for (size_t index = 0; index < sizeof value; ++index) bytes[index] = static_cast<unsigned char>(value >> (8 * index));
    addBytes(bytes, sizeof bytes);
}

void StateHasher::addText(std::string_view text) {
    addU64(text.size());
    addBytes(text.data(), text.size());
}

void StateHasher::addFloat(float value) { addI64(quantiseForHash(value)); }

void StateHasher::addVec3(float x, float y, float z) {
    addFloat(x);
    addFloat(y);
    addFloat(z);
}

int64_t quantiseForHash(float value) {
    if (!std::isfinite(value)) return kNonFiniteSentinel;
    const double scaled = std::round(static_cast<double>(value) / static_cast<double>(kHashQuantum));
    if (scaled > kQuantiseLimit) return static_cast<int64_t>(kQuantiseLimit);
    if (scaled < -kQuantiseLimit) return -static_cast<int64_t>(kQuantiseLimit);
    return static_cast<int64_t>(scaled);
}

uint64_t hashWorldState(const World& world, const HashSources& sources) {
    StateHasher hasher;
    hasher.addU64(kSectionWorld);
    hasher.addU64(world.time().frame);
    hasher.addU64(world.aliveCount());
    world.forEach([&](EntityHandle entity) { hashEntity(hasher, world, entity); });
    hasher.addU64(kSectionRandom);
    hashRandom(hasher, world.random());
    if (sources.state) {
        hasher.addU64(kSectionState);
        hasher.addText(sources.state->toJson().dump());
    }
    if (sources.storylets) {
        hasher.addU64(kSectionStorylets);
        hasher.addText(sources.storylets->toJson().dump());
    }
    if (sources.params) {
        hasher.addU64(kSectionParams);
        hashParams(hasher, *sources.params);
    }
    if (sources.nav) {
        hasher.addU64(kSectionNav);
        hasher.addU64(sources.nav->hash());
    }
    hasher.addU64(kSectionExtra);
    for (const uint64_t extra : sources.extra) hasher.addU64(extra);
    return hasher.value();
}
