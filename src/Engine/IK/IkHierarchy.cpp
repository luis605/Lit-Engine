module;

#include <algorithm>
#include <cctype>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.World;

namespace {

constexpr const char* kVisualTagName = "ik.visual";
constexpr float kMaxRecipeNumber = 1.0e6f;

bool startsWith(const std::string& text, const std::string& prefix, bool ignoreCase) {
    if (text.size() < prefix.size()) return false;
    for (size_t index = 0; index < prefix.size(); ++index) {
        const char left = ignoreCase ? static_cast<char>(std::tolower(static_cast<unsigned char>(text[index]))) : text[index];
        const char right = ignoreCase ? static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[index]))) : prefix[index];
        if (left != right) return false;
    }
    return true;
}

EntityHandle nextJoint(World& world, EntityHandle joint) {
    EntityHandle fallback = NULL_ENTITY;
    for (EntityHandle child : world.getChildren(joint)) {
        if (world.getTag(child) == kVisualTagName) continue;
        if (!world.getChildren(child).empty()) return child;
        if (fallback.isNull()) fallback = child;
    }
    return fallback;
}

std::vector<EntityHandle> followChain(World& world, EntityHandle hip) {
    std::vector<EntityHandle> path{hip};
    while (path.size() < static_cast<size_t>(kMaxIkJoints)) {
        const EntityHandle next = nextJoint(world, path.back());
        if (next.isNull()) break;
        path.push_back(next);
    }
    return path;
}

EntityHandle ensureFootMarker(World& world, IkChain* existing, const std::vector<EntityHandle>& joints) {
    if (existing && world.isAlive(existing->target)) return existing->target;
    EntityDesc desc;
    desc.name = world.getName(joints.front()) + ".target";
    desc.position = world.getWorldPosition(joints.back());
    return world.create(desc);
}

struct RecipeValues {
    std::string kind = "spider";
    SpiderOptions options;
};

bool parseNumber(std::string_view text, float& out) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc() && std::isfinite(out) && std::abs(out) <= kMaxRecipeNumber;
}

void applyKind(RecipeValues& values) {
    SpiderOptions& options = values.options;
    if (values.kind == "quadruped") {
        options.legs = 4;
        options.segments = 3;
        options.heightRatio = 0.7f;
        options.restRatio = 0.78f;
        options.splay = 0.1f;
        options.bodyRadius = 0.25f;
        options.bodyLength = 1.0f;
    } else if (values.kind == "biped") {
        options.legs = 2;
        options.segments = 3;
        options.heightRatio = 0.75f;
        options.restRatio = 0.8f;
        options.splay = 0.0f;
        options.bodyRadius = 0.2f;
        options.bodyLength = 0.1f;
    } else if (values.kind == "hexapod") {
        options.legs = 6;
    }
    options.name = values.kind;
}

void applyToken(RecipeValues& values, std::string_view key, std::string_view text) {
    float number = 0.0f;
    if (!parseNumber(text, number)) return;
    SpiderOptions& options = values.options;
    if (key == "legs") options.legs = static_cast<int>(number);
    else if (key == "segments") options.segments = static_cast<int>(number);
    else if (key == "length") options.legLength = number;
    else if (key == "radius") options.bodyRadius = number;
    else if (key == "bodylength") options.bodyLength = number;
    else if (key == "speed") options.maxSpeed = number;
    else if (key == "stride") options.stride = number;
    else if (key == "step") options.stepHeight = number;
    else if (key == "thickness") options.thickness = number;
}

}

std::vector<std::vector<EntityHandle>> detectLegs(World& world, EntityHandle body, const LegPattern& pattern) {
    std::vector<std::vector<EntityHandle>> legs;
    for (EntityHandle child : world.getChildren(body)) {
        if (world.getTag(child) == kVisualTagName || !startsWith(world.getName(child), pattern.prefix, pattern.ignoreCase)) continue;
        std::vector<EntityHandle> path = followChain(world, child);
        if (path.size() >= 2) legs.push_back(std::move(path));
    }
    return legs;
}

bool makeWalkerFromHierarchy(World& world, EntityHandle body, const LegPattern& pattern) {
    if (!world.isAlive(body)) return false;
    const std::vector<std::vector<EntityHandle>> legs = detectLegs(world, body, pattern);
    if (legs.empty() || legs.size() > kMaxWalkerLegs) return false;
    Walker walker;
    walker.body = body;
    for (const std::vector<EntityHandle>& joints : legs) {
        IkChain* existing = world.get<IkChain>(joints.front());
        const EntityHandle marker = ensureFootMarker(world, existing, joints);
        createIkChain(world, joints, marker);
        walker.legs.push_back(WalkerLeg{.chain = joints.front(), .foot = marker});
    }
    world.add<Walker>(body, std::move(walker));
    return true;
}

EntityHandle makeWalkerFromRecipe(World& world, const glm::vec3& position, std::string_view recipe, uint32_t bodyMesh, uint32_t boneMesh) {
    RecipeValues values;
    size_t cursor = 0;
    bool first = true;
    std::vector<std::pair<std::string, std::string>> pairs;
    while (cursor < recipe.size()) {
        const size_t end = std::min(recipe.find(' ', cursor), recipe.size());
        const std::string_view token = recipe.substr(cursor, end - cursor);
        cursor = end + 1;
        if (token.empty()) continue;
        const size_t equals = token.find('=');
        if (equals == std::string_view::npos && first) values.kind = std::string(token);
        else if (equals != std::string_view::npos) pairs.emplace_back(std::string(token.substr(0, equals)), std::string(token.substr(equals + 1)));
        first = false;
    }
    applyKind(values);
    for (const auto& [key, text] : pairs) applyToken(values, key, text);
    values.options.bodyMesh = bodyMesh;
    values.options.boneMesh = boneMesh;
    return makeSpider(world, position, values.options);
}

std::vector<EntityHandle> collectRigEntities(World& world, EntityHandle root) {
    std::vector<EntityHandle> entities;
    if (!world.isAlive(root)) return entities;
    entities.push_back(root);
    for (size_t index = 0; index < entities.size(); ++index) {
        for (const EntityHandle child : world.getChildren(entities[index])) entities.push_back(child);
    }
    if (const Walker* walker = world.get<Walker>(root)) {
        for (const WalkerLeg& leg : walker->legs) {
            if (world.isAlive(leg.foot)) entities.push_back(leg.foot);
        }
    }
    return entities;
}
