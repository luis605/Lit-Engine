module;

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

module Engine.Delta;

import Engine.glm;
import Engine.Json;
import Engine.Portal;
import Engine.World;

#include "DeltaCodec.hpp"

namespace delta_codec {
constexpr int kMatrixSide = 4;
constexpr size_t kVec3Size = 3;
constexpr size_t kMatrixSize = 16;
constexpr size_t kComponentPairSize = 2;
constexpr size_t kStateFieldCount = 11;
constexpr double kMaxUint32 = 4294967295.0;

uint32_t safeUint32(double value) { return value >= 0.0 && value <= kMaxUint32 ? static_cast<uint32_t>(value) : 0; }

int safeInt(double value, int fallback) { return value >= -2147483648.0 && value <= 2147483647.0 ? static_cast<int>(value) : fallback; }

Json vec3Json(const glm::vec3& vector) {
    Json array = Json::array();
    array.reserve(kVec3Size);
    array.push(vector.x);
    array.push(vector.y);
    array.push(vector.z);
    return array;
}

glm::vec3 vec3From(const Json* json, glm::vec3 fallback) {
    if (!json || json->items().size() != kVec3Size) return fallback;
    return {static_cast<float>(json->at(0)->asNumber()), static_cast<float>(json->at(1)->asNumber()), static_cast<float>(json->at(2)->asNumber())};
}

Json componentsJson(const std::vector<std::pair<std::string, std::string>>& components) {
    Json out = Json::array();
    out.reserve(components.size());
    for (const auto& [name, text] : components) {
        Json pair = Json::array();
        pair.reserve(kComponentPairSize);
        pair.push(name);
        pair.push(text);
        out.push(std::move(pair));
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> componentsFrom(const Json* json) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!json) return out;
    for (const Json& pair : json->items()) {
        if (pair.items().size() == kComponentPairSize) out.emplace_back(pair.at(0)->asString(), pair.at(1)->asString());
    }
    return out;
}

Json matrixJson(const glm::mat4& matrix) {
    Json array = Json::array();
    array.reserve(kMatrixSize);
    for (int column = 0; column < kMatrixSide; ++column) {
        for (int row = 0; row < kMatrixSide; ++row) array.push(matrix[column][row]);
    }
    return array;
}

glm::mat4 matrixFrom(const Json* json) {
    glm::mat4 matrix(1.0f);
    if (!json || json->items().size() != kMatrixSize) return matrix;
    for (int column = 0; column < kMatrixSide; ++column) {
        for (int row = 0; row < kMatrixSide; ++row) matrix[column][row] = static_cast<float>(json->at(static_cast<size_t>(column * kMatrixSide + row))->asNumber());
    }
    return matrix;
}

Json stateJson(const EntityState& state) {
    Json json = Json::object();
    json.reserve(kStateFieldCount);
    json.set("name", state.name);
    json.set("tag", state.tag);
    json.set("mesh", state.mesh);
    json.set("material", state.material);
    json.set("shader", state.shader);
    json.set("layer", state.layer);
    json.set("flags", state.renderFlags);
    json.set("alpha", state.alpha);
    json.set("visible", state.visible);
    json.set("local", matrixJson(state.local));
    json.set("components", componentsJson(state.components));
    return json;
}

EntityState stateFrom(const Json& json) {
    EntityState state;
    if (const Json* value = json.get("name")) state.name = value->asString();
    if (const Json* value = json.get("tag")) state.tag = value->asString();
    if (const Json* value = json.get("mesh")) state.mesh = safeUint32(value->asNumber());
    if (const Json* value = json.get("material")) state.material = safeUint32(value->asNumber());
    if (const Json* value = json.get("shader")) state.shader = safeUint32(value->asNumber());
    if (const Json* value = json.get("layer")) state.layer = safeUint32(value->asNumber());
    if (const Json* value = json.get("flags")) state.renderFlags = safeUint32(value->asNumber());
    if (const Json* value = json.get("alpha")) state.alpha = static_cast<float>(value->asNumber(1.0));
    if (const Json* value = json.get("visible")) state.visible = value->asBool(true);
    state.local = matrixFrom(json.get("local"));
    state.components = componentsFrom(json.get("components"));
    return state;
}

Json prefabJson(const Prefab& prefab) {
    Json nodes = Json::array();
    for (const PrefabNode& node : prefab.nodes) {
        EntityState state;
        state.name = node.name;
        state.tag = node.tag;
        state.mesh = node.mesh;
        state.material = node.material;
        state.shader = node.shader;
        state.layer = node.layer;
        state.renderFlags = node.renderFlags;
        state.alpha = node.alpha;
        state.visible = node.visible;
        state.local = node.local;
        state.components = node.components;
        Json json = stateJson(state);
        json.set("parent", node.parent);
        json.set("source", node.sourceIndex);
        nodes.push(std::move(json));
    }
    return nodes;
}

Prefab prefabFrom(const Json& nodes) {
    Prefab prefab;
    for (const Json& json : nodes.items()) {
        EntityState state = stateFrom(json);
        PrefabNode node;
        node.name = std::move(state.name);
        node.tag = std::move(state.tag);
        node.mesh = state.mesh;
        node.material = state.material;
        node.shader = state.shader;
        node.layer = state.layer;
        node.renderFlags = state.renderFlags;
        node.alpha = state.alpha;
        node.visible = state.visible;
        node.local = state.local;
        node.components = std::move(state.components);
        if (const Json* value = json.get("parent")) node.parent = safeInt(value->asNumber(-1), -1);
        if (const Json* value = json.get("source")) node.sourceIndex = safeUint32(value->asNumber());
        prefab.nodes.push_back(std::move(node));
    }
    return prefab;
}

Json paramsJson(const PortalCellParams& params) {
    Json json = Json::object();
    json.set("gravity", vec3Json(params.gravity));
    json.set("tint", vec3Json(params.tint));
    json.set("sun", params.sun);
    json.set("ambient", params.ambient);
    Json scalars = Json::object();
    for (const auto& [name, value] : params.scalars) scalars.set(name, value);
    json.set("scalars", std::move(scalars));
    return json;
}

bool sameParams(const PortalCellParams& left, const PortalCellParams& right) {
    return left.gravity == right.gravity && left.tint == right.tint && left.sun == right.sun && left.ambient == right.ambient && left.scalars == right.scalars;
}

void paramsFrom(const Json& json, PortalCellParams& params) {
    params.gravity = vec3From(json.get("gravity"), params.gravity);
    params.tint = vec3From(json.get("tint"), params.tint);
    if (const Json* value = json.get("sun")) params.sun = static_cast<float>(value->asNumber(1.0));
    if (const Json* value = json.get("ambient")) params.ambient = static_cast<float>(value->asNumber(1.0));
    if (const Json* scalars = json.get("scalars")) {
        params.scalars.clear();
        for (const auto& [name, value] : scalars->members()) params.scalars[name] = static_cast<float>(value.asNumber());
    }
}
}
