module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

module Engine.IK;

import Engine.glm;
import Engine.Json;
import Engine.Render.entity;
import Engine.World;

namespace {

constexpr float kRadiansPerDegree = 3.14159265358979f / 180.0f;
constexpr float kMaxJsonNumber = 1.0e30f;
constexpr float kMaxJsonInt = 1.0e6f;

float numberOr(const Json& object, std::string_view key, float fallback) {
    const Json* value = object.get(key);
    if (!value || !value->isNumber()) return fallback;
    const double number = value->asNumber();
    return std::abs(number) <= static_cast<double>(kMaxJsonNumber) ? static_cast<float>(number) : fallback;
}

int intOr(const Json& object, std::string_view key, int fallback) {
    return static_cast<int>(std::clamp(numberOr(object, key, static_cast<float>(fallback)), -kMaxJsonInt, kMaxJsonInt));
}

std::string stringOr(const Json& object, std::string_view key, const std::string& fallback) {
    const Json* value = object.get(key);
    return value && value->isString() ? value->asString() : fallback;
}

std::optional<glm::vec3> readPoint(const Json* value) {
    if (!value || !value->isArray() || value->size() != 3) return std::nullopt;
    return glm::vec3(static_cast<float>(value->at(0)->asNumber()), static_cast<float>(value->at(1)->asNumber()), static_cast<float>(value->at(2)->asNumber()));
}

Json writePoint(const glm::vec3& point) {
    Json array = Json::array();
    array.push(point.x);
    array.push(point.y);
    array.push(point.z);
    return array;
}

IkAlgorithm algorithmFromName(const std::string& name) {
    if (name == "twobone") return IkAlgorithm::TwoBone;
    if (name == "fabrik") return IkAlgorithm::Fabrik;
    if (name == "ccd") return IkAlgorithm::Ccd;
    return IkAlgorithm::Auto;
}

const char* algorithmName(IkAlgorithm algorithm) {
    switch (algorithm) {
    case IkAlgorithm::TwoBone: return "twobone";
    case IkAlgorithm::Fabrik: return "fabrik";
    case IkAlgorithm::Ccd: return "ccd";
    default: return "auto";
    }
}

IkJointLimit readLimit(const Json& json) {
    IkJointLimit limit;
    limit.hinge = json.get("hinge") && json.get("hinge")->asBool();
    limit.minAngle = numberOr(json, "min", -180.0f) * kRadiansPerDegree;
    limit.maxAngle = numberOr(json, "max", 180.0f) * kRadiansPerDegree;
    limit.cone = numberOr(json, "cone", 180.0f) * kRadiansPerDegree;
    return limit;
}

void bindTarget(World& world, const Json& spec, EntityHandle& handle, glm::vec3& point, bool& usePoint) {
    if (spec.isString()) handle = world.find(spec.asString());
    else if (const auto literal = readPoint(&spec)) {
        point = *literal;
        usePoint = true;
    }
}

bool applyChain(World& world, const Json& spec) {
    const Json* names = spec.get("joints");
    if (!names || !names->isArray()) return false;
    std::vector<EntityHandle> joints;
    for (const Json& name : names->items()) joints.push_back(world.find(name.asString()));
    for (EntityHandle joint : joints) {
        if (!world.isAlive(joint)) return false;
    }
    if (createIkChain(world, joints).isNull()) return false;
    IkChain* chain = world.get<IkChain>(joints.front());
    if (const Json* target = spec.get("target")) bindTarget(world, *target, chain->target, chain->targetPoint, chain->useTargetPoint);
    if (const Json* pole = spec.get("pole")) bindTarget(world, *pole, chain->pole, chain->polePoint, chain->usePolePoint);
    chain->algorithm = algorithmFromName(stringOr(spec, "algorithm", "auto"));
    chain->weight = numberOr(spec, "weight", 1.0f);
    chain->iterations = intOr(spec, "iterations", 12);
    if (const Json* limits = spec.get("limits")) {
        for (const Json& limit : limits->items()) chain->limits.push_back(readLimit(limit));
        chain->limits.resize(chain->joints.size());
    }
    return true;
}

SpiderOptions readOptions(const Json& rig) {
    SpiderOptions options;
    options.name = stringOr(rig, "name", options.name);
    options.legs = intOr(rig, "legs", options.legs);
    options.segments = intOr(rig, "segments", options.segments);
    options.legLength = numberOr(rig, "legLength", options.legLength);
    options.bodyRadius = numberOr(rig, "bodyRadius", options.bodyRadius);
    options.bodyLength = numberOr(rig, "bodyLength", options.bodyLength);
    options.thickness = numberOr(rig, "thickness", options.thickness);
    options.stride = numberOr(rig, "stride", options.stride);
    options.stepHeight = numberOr(rig, "stepHeight", options.stepHeight);
    options.maxSpeed = numberOr(rig, "maxSpeed", options.maxSpeed);
    options.heightRatio = numberOr(rig, "heightRatio", options.heightRatio);
    options.restRatio = numberOr(rig, "restRatio", options.restRatio);
    options.splay = numberOr(rig, "splay", options.splay);
    return options;
}

Json chainToJson(World& world, const IkChain& chain) {
    Json spec = Json::object();
    Json joints = Json::array();
    for (EntityHandle joint : chain.joints) joints.push(world.getName(joint));
    spec.set("joints", joints);
    if (world.isAlive(chain.target)) spec.set("target", world.getName(chain.target));
    else if (chain.useTargetPoint) spec.set("target", writePoint(chain.targetPoint));
    if (world.isAlive(chain.pole)) spec.set("pole", world.getName(chain.pole));
    spec.set("algorithm", algorithmName(chain.algorithm));
    spec.set("weight", chain.weight);
    spec.set("iterations", chain.iterations);
    return spec;
}

}

std::optional<EntityHandle> loadRigJson(World& world, const Json& rig, const glm::vec3& position, std::string* error) {
    if (!rig.isObject()) {
        if (error) *error = "rig must be an object";
        return std::nullopt;
    }
    EntityHandle root = NULL_ENTITY;
    if (rig.get("legs")) {
        root = makeSpider(world, position, readOptions(rig));
    } else if (const Json* hierarchy = rig.get("hierarchy")) {
        root = world.find(hierarchy->asString());
        if (!world.isAlive(root) || !makeWalkerFromHierarchy(world, root, LegPattern{stringOr(rig, "pattern", "leg"), true})) {
            if (error) *error = "hierarchy not found or no legs detected";
            return std::nullopt;
        }
    }
    if (const Json* chains = rig.get("chains")) {
        for (const Json& spec : chains->items()) {
            if (!applyChain(world, spec) && error) *error = "invalid chain";
        }
    }
    if (const Json* tuning = rig.get("walker"); tuning && world.isAlive(root)) {
        if (Walker* walker = world.get<Walker>(root)) {
            walker->maxSpeed = numberOr(*tuning, "maxSpeed", walker->maxSpeed);
            walker->stride = numberOr(*tuning, "stride", walker->stride);
            walker->stepHeight = numberOr(*tuning, "stepHeight", walker->stepHeight);
        }
    }
    if (!world.isAlive(root) && !(error && !error->empty())) {
        if (error) *error = "rig has no legs, hierarchy or chains";
        return std::nullopt;
    }
    return root;
}

Json rigToJson(World& world, EntityHandle root) {
    Json rig = Json::object();
    if (const Walker* walker = world.get<Walker>(root)) {
        rig.set("hierarchy", world.getName(root));
        Json tuning = Json::object();
        tuning.set("maxSpeed", walker->maxSpeed);
        tuning.set("stride", walker->stride);
        tuning.set("stepHeight", walker->stepHeight);
        rig.set("walker", tuning);
    }
    Json chains = Json::array();
    world.view<IkChain>([&](EntityHandle, IkChain& chain) {
        if (!chain.joints.empty() && (chain.joints.front() == root || world.isDescendantOf(chain.joints.front(), root))) chains.push(chainToJson(world, chain));
    });
    rig.set("chains", chains);
    return rig;
}
