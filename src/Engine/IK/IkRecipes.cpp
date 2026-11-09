module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

module Engine.IK;

import Engine.glm;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.World;

namespace {

constexpr float kReachFraction = 0.9f;
constexpr float kStrideFraction = 0.8f;
constexpr float kStepHeightFraction = 0.18f;
constexpr float kRootInset = 0.7f;
constexpr float kPoleScale = 1.0f;

float positiveOr(float value, float fallback) { return std::isfinite(value) && value > 0.0f ? value : fallback; }

struct LegLayout {
    glm::vec3 hip{0.0f};
    glm::vec3 outward{1.0f, 0.0f, 0.0f};
    glm::vec3 foot{0.0f};
};

LegLayout layoutLeg(const SpiderOptions& options, const glm::vec3& bodyPosition, int index, float drop, float horizontalReach) {
    const int rows = (options.legs + 1) / 2;
    const float side = index % 2 == 0 ? 1.0f : -1.0f;
    const float t = rows > 1 ? static_cast<float>(index / 2) / static_cast<float>(rows - 1) : 0.5f;
    const float angle = (t - 0.5f) * 2.0f * options.splay;
    LegLayout layout;
    layout.outward = glm::vec3(side * std::cos(angle), 0.0f, std::sin(angle));
    layout.hip = bodyPosition + glm::vec3(side * options.bodyRadius * kRootInset, 0.0f, (t - 0.5f) * options.bodyLength);
    layout.foot = layout.hip + layout.outward * horizontalReach - glm::vec3(0.0f, drop, 0.0f);
    return layout;
}

std::vector<glm::vec3> poseLeg(const LegLayout& layout, int segments, float boneLength) {
    IkChainState state;
    state.count = segments + 1;
    const glm::vec3 span = layout.foot - layout.hip;
    for (int joint = 0; joint < state.count; ++joint) {
        state.points[static_cast<size_t>(joint)] = layout.hip + span * (static_cast<float>(joint) / static_cast<float>(segments));
        if (joint < segments) state.lengths[static_cast<size_t>(joint)] = boneLength;
    }
    IkSettings settings;
    settings.hasPole = true;
    settings.pole = layout.hip + glm::vec3(0.0f, boneLength * static_cast<float>(segments) * kPoleScale, 0.0f) + layout.outward * boneLength;
    settings.iterations = 24;
    solveIk(state, layout.foot, settings);
    return std::vector<glm::vec3>(state.points.begin(), state.points.begin() + state.count);
}

EntityHandle createJoint(World& world, const std::string& name, const glm::vec3& position, const glm::quat& rotation) {
    EntityDesc desc;
    desc.name = name;
    desc.position = position;
    desc.rotation = rotation;
    return world.create(desc);
}

void addBoneVisual(World& world, EntityHandle joint, float length, const SpiderOptions& options) {
    if (options.boneMesh == NO_MESH) return;
    EntityDesc desc;
    desc.name = world.getName(joint) + ".mesh";
    desc.mesh = options.boneMesh;
    desc.material = options.material;
    desc.parent = joint;
    desc.position = glm::vec3(0.0f, -options.thickness * 0.5f, -options.thickness * 0.5f);
    desc.scale = glm::vec3(length, options.thickness, options.thickness);
    world.setTag(world.create(desc), "ik.visual");
}

std::vector<EntityHandle> buildLegJoints(World& world, EntityHandle body, const std::string& legName, const std::vector<glm::vec3>& points, const SpiderOptions& options) {
    std::vector<EntityHandle> joints;
    for (size_t index = 0; index < points.size(); ++index) {
        const size_t boneIndex = std::min(index, points.size() - 2);
        const glm::vec3 direction = glm::normalize(points[boneIndex + 1] - points[boneIndex]);
        const std::string name = index == 0 ? legName : legName + "." + std::to_string(index);
        joints.push_back(createJoint(world, name, points[index], ikRotationBetween(glm::vec3(1.0f, 0.0f, 0.0f), direction)));
    }
    for (size_t index = 0; index < joints.size(); ++index) {
        world.setParent(joints[index], index == 0 ? body : joints[index - 1], true);
        if (index + 1 < points.size()) addBoneVisual(world, joints[index], glm::length(points[index + 1] - points[index]), options);
    }
    return joints;
}

EntityHandle createFootMarker(World& world, const std::string& legName, const glm::vec3& position) {
    EntityDesc desc;
    desc.name = legName + ".target";
    desc.position = position;
    return world.create(desc);
}

void addBodyVisual(World& world, EntityHandle body, const SpiderOptions& options) {
    if (options.bodyMesh == NO_MESH) return;
    EntityDesc desc;
    desc.name = options.name + ".body";
    desc.mesh = options.bodyMesh;
    desc.material = options.material;
    desc.parent = body;
    desc.scale = glm::vec3(options.bodyRadius * 2.0f, options.bodyRadius, options.bodyLength + options.bodyRadius);
    world.setTag(world.create(desc), "ik.visual");
}

void configureWalker(Walker& walker, const SpiderOptions& options, float drop, float horizontalReach) {
    const float maxHorizontal = std::sqrt(std::max(0.0f, (kReachFraction * options.legLength) * (kReachFraction * options.legLength) - drop * drop));
    walker.bodyHeight = drop;
    walker.stride = options.stride > 0.0f ? options.stride : std::max(0.02f, kStrideFraction * (maxHorizontal - horizontalReach));
    walker.stepHeight = options.stepHeight > 0.0f ? options.stepHeight : kStepHeightFraction * options.legLength;
    walker.maxSpeed = options.maxSpeed;
}

}

EntityHandle makeSpider(World& world, const glm::vec3& position, const SpiderOptions& options) {
    const SpiderOptions defaults;
    SpiderOptions resolved = options;
    resolved.legLength = positiveOr(options.legLength, defaults.legLength);
    resolved.heightRatio = positiveOr(options.heightRatio, defaults.heightRatio);
    resolved.restRatio = positiveOr(options.restRatio, defaults.restRatio);
    resolved.bodyRadius = positiveOr(options.bodyRadius, defaults.bodyRadius);
    resolved.bodyLength = positiveOr(options.bodyLength, defaults.bodyLength);
    resolved.maxSpeed = positiveOr(options.maxSpeed, defaults.maxSpeed);
    resolved.splay = std::isfinite(options.splay) ? options.splay : defaults.splay;
    resolved.thickness = positiveOr(options.thickness, defaults.thickness);
    resolved.legs = std::clamp(options.legs, 2, static_cast<int>(kMaxWalkerLegs));
    resolved.segments = std::clamp(options.segments, 2, kMaxIkJoints - 1);
    const float drop = resolved.heightRatio * resolved.legLength;
    const float rest = resolved.restRatio * resolved.legLength;
    const float horizontalReach = std::sqrt(std::max(0.0f, rest * rest - drop * drop));
    const float boneLength = resolved.legLength / static_cast<float>(resolved.segments);

    EntityDesc bodyDesc;
    bodyDesc.name = resolved.name;
    bodyDesc.position = position + glm::vec3(0.0f, drop, 0.0f);
    const EntityHandle body = world.create(bodyDesc);
    addBodyVisual(world, body, resolved);

    Walker walker;
    walker.body = body;
    configureWalker(walker, resolved, drop, horizontalReach);
    for (int index = 0; index < resolved.legs; ++index) {
        const LegLayout layout = layoutLeg(resolved, bodyDesc.position, index, drop, horizontalReach);
        const std::string legName = "leg" + std::to_string(index);
        const std::vector<EntityHandle> joints = buildLegJoints(world, body, legName, poseLeg(layout, resolved.segments, boneLength), resolved);
        const EntityHandle marker = createFootMarker(world, legName, layout.foot);
        createIkChain(world, joints, marker);
        walker.legs.push_back(WalkerLeg{.chain = joints.front(), .foot = marker});
    }
    world.add<Walker>(body, std::move(walker));
    return body;
}
