module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <vector>

module Engine.IkTestScene;

import Engine.glm;
import Engine.World;
import Engine.IK;
import Engine.Render.entity;

namespace {

constexpr float kBoneThickness = 0.12f;
constexpr float kHeadRadius = 0.28f;
constexpr float kPedestalSize = 1.0f;

struct ChainSpec {
    std::string name;
    glm::vec3 base{0.0f};
    glm::vec3 direction{0.0f, 1.0f, 0.0f};
    int bones = 2;
    float boneLength = 1.0f;
    float thickness = kBoneThickness;
    uint32_t material = 0;
    IkAlgorithm algorithm = IkAlgorithm::Auto;
    int iterations = 12;
    bool hasPole = false;
    glm::vec3 pole{0.0f};
    EntityHandle target = NULL_ENTITY;
};

std::vector<EntityHandle> buildChain(World& world, const IkTestKit& kit, const ChainSpec& spec) {
    const glm::quat rotation = ikRotationBetween(glm::vec3(1.0f, 0.0f, 0.0f), glm::normalize(spec.direction));
    std::vector<EntityHandle> joints;
    for (int index = 0; index <= spec.bones; ++index) {
        EntityDesc joint;
        joint.name = index == 0 ? spec.name : spec.name + "." + std::to_string(index);
        joint.position = spec.base + glm::normalize(spec.direction) * (spec.boneLength * static_cast<float>(index));
        joint.rotation = rotation;
        joints.push_back(world.create(joint));
    }
    for (int index = 0; index <= spec.bones; ++index) {
        if (index > 0) world.setParent(joints[static_cast<size_t>(index)], joints[static_cast<size_t>(index - 1)], true);
        if (index == spec.bones) continue;
        EntityDesc visual;
        visual.name = world.getName(joints[static_cast<size_t>(index)]) + ".mesh";
        visual.mesh = kit.cube;
        visual.material = spec.material;
        visual.parent = joints[static_cast<size_t>(index)];
        visual.position = glm::vec3(0.0f, -spec.thickness * 0.5f, -spec.thickness * 0.5f);
        visual.scale = glm::vec3(spec.boneLength, spec.thickness, spec.thickness);
        world.setTag(world.create(visual), "ik.visual");
    }
    createIkChain(world, joints, spec.target);
    IkChain* chain = world.get<IkChain>(joints.front());
    chain->algorithm = spec.algorithm;
    chain->iterations = spec.iterations;
    chain->usePolePoint = spec.hasPole;
    chain->polePoint = spec.pole;
    return joints;
}

EntityHandle addHead(World& world, const IkTestKit& kit, EntityHandle neckTip, uint32_t material) {
    EntityDesc head;
    head.name = "head";
    head.mesh = kit.sphere;
    head.material = material;
    head.parent = neckTip;
    head.scale = glm::vec3(kHeadRadius);
    const EntityHandle entity = world.create(head);
    world.setTag(entity, "ik.visual");
    EntityDesc nose;
    nose.name = "nose";
    nose.mesh = kit.sphere;
    nose.material = material;
    nose.parent = neckTip;
    nose.position = glm::vec3(kHeadRadius * 0.9f, 0.0f, 0.0f);
    nose.scale = glm::vec3(kHeadRadius * 0.35f);
    world.setTag(world.create(nose), "ik.visual");
    return entity;
}

EntityHandle marker(World& world, const IkTestKit& kit, const char* name, uint32_t material, const glm::vec3& center, float radius) {
    EntityDesc description;
    description.name = name;
    description.mesh = kit.sphere;
    description.material = material;
    description.position = center;
    description.scale = glm::vec3(radius);
    return world.create(description);
}

void buildArm(World& world, const IkTestKit& kit) {
    const uint32_t stone = ikTestPaint(kit, glm::vec3(0.5f), 0.7f);
    const uint32_t metal = ikTestPaint(kit, glm::vec3(0.95f, 0.5f, 0.15f), 0.3f, 1.0f);
    const uint32_t ball = ikTestGlow(kit, glm::vec3(1.0f, 0.55f, 0.1f), 4.0f);
    const float pedestalHeight = 1.2f;
    ikTestStaticBox(world, kit, "arm pedestal", stone, glm::vec3(-9.5f, 0.0f, -0.5f), glm::vec3(kPedestalSize, pedestalHeight, kPedestalSize));
    const EntityHandle target = ikTestPushableSphere(world, kit, "arm target", ball, glm::vec3(-7.4f, 0.4f, 0.6f), 0.4f);
    ChainSpec spec;
    spec.name = "arm";
    spec.base = glm::vec3(-9.0f, pedestalHeight, 0.0f);
    spec.direction = glm::vec3(0.0f, 1.0f, 0.0f);
    spec.bones = 2;
    spec.boneLength = 1.5f;
    spec.thickness = 0.2f;
    spec.material = metal;
    spec.algorithm = IkAlgorithm::TwoBone;
    spec.hasPole = true;
    spec.pole = glm::vec3(-9.0f, 4.0f, -1.5f);
    spec.target = target;
    buildChain(world, kit, spec);
}

void buildLookHead(World& world, const IkTestKit& kit) {
    const uint32_t stone = ikTestPaint(kit, glm::vec3(0.6f, 0.58f, 0.55f), 0.7f);
    const uint32_t skin = ikTestPaint(kit, glm::vec3(0.9f, 0.75f, 0.6f), 0.5f);
    const uint32_t eye = ikTestGlow(kit, glm::vec3(0.4f, 0.8f, 1.0f), 4.0f);
    constexpr float columnHeight = 1.6f;
    ikTestStaticBox(world, kit, "head column", stone, glm::vec3(2.5f, 0.0f, 5.5f), glm::vec3(0.8f, columnHeight, 0.8f));
    const EntityHandle target = marker(world, kit, kIkTestLookTargetName, eye, glm::vec3(3.0f, 2.8f, 11.0f), 0.15f);
    ChainSpec spec;
    spec.name = "neck";
    spec.base = glm::vec3(2.9f, columnHeight, 5.9f);
    spec.bones = 1;
    spec.boneLength = 0.55f;
    spec.thickness = 0.18f;
    spec.material = skin;
    spec.algorithm = IkAlgorithm::Fabrik;
    spec.target = target;
    const std::vector<EntityHandle> neck = buildChain(world, kit, spec);
    addHead(world, kit, neck.back(), skin);
}

void buildTentacle(World& world, const IkTestKit& kit) {
    const uint32_t purple = ikTestPaint(kit, glm::vec3(0.55f, 0.15f, 0.7f), 0.4f);
    const uint32_t ball = ikTestGlow(kit, glm::vec3(0.8f, 0.3f, 1.0f), 4.0f);
    const EntityHandle target = ikTestPushableSphere(world, kit, "tentacle target", ball, glm::vec3(-6.4f, 0.35f, 10.2f), 0.35f);
    ChainSpec spec;
    spec.name = "tentacle";
    spec.base = glm::vec3(-9.0f, 0.0f, 12.0f);
    spec.bones = 8;
    spec.boneLength = 0.6f;
    spec.thickness = 0.14f;
    spec.material = purple;
    spec.algorithm = IkAlgorithm::Fabrik;
    spec.iterations = 16;
    spec.target = target;
    buildChain(world, kit, spec);
}

void buildCcdChain(World& world, const IkTestKit& kit) {
    const uint32_t teal = ikTestPaint(kit, glm::vec3(0.1f, 0.6f, 0.6f), 0.35f, 0.6f);
    const uint32_t ball = ikTestGlow(kit, glm::vec3(0.2f, 1.0f, 0.9f), 4.0f);
    const EntityHandle target = ikTestPushableSphere(world, kit, "ccd target", ball, glm::vec3(-1.4f, 0.35f, 8.8f), 0.35f);
    ChainSpec spec;
    spec.name = "ccd";
    spec.base = glm::vec3(-3.0f, 0.0f, 10.5f);
    spec.bones = 5;
    spec.boneLength = 0.7f;
    spec.thickness = 0.16f;
    spec.material = teal;
    spec.algorithm = IkAlgorithm::Ccd;
    spec.iterations = 20;
    spec.target = target;
    buildChain(world, kit, spec);
}

void paint(World& world, EntityHandle root, uint32_t material) {
    for (const EntityHandle entity : collectRigEntities(world, root)) world.setMaterial(entity, material);
}

void buildWalkers(World& world, const IkTestKit& kit) {
    const uint32_t spiderPaint = ikTestPaint(kit, glm::vec3(0.12f, 0.1f, 0.1f), 0.35f, 0.5f);
    const uint32_t quadPaint = ikTestPaint(kit, glm::vec3(0.75f, 0.45f, 0.2f), 0.6f);
    const uint32_t bipedPaint = ikTestPaint(kit, glm::vec3(0.3f, 0.5f, 0.85f), 0.5f);
    const uint32_t goalPaint = ikTestGlow(kit, glm::vec3(0.3f, 0.7f, 1.0f), 4.0f);

    SpiderOptions options;
    options.legs = 8;
    options.legLength = 1.2f;
    options.maxSpeed = 2.2f;
    options.bodyMesh = kit.sphere;
    options.boneMesh = kit.cube;
    options.material = spiderPaint;
    options.name = kIkTestSpiderName;
    const EntityHandle spider = makeSpider(world, glm::vec3(-4.0f, 0.0f, 4.0f), options);
    setWalkerTarget(world, spider, glm::vec3(0.0f, 0.0f, 12.0f));

    const EntityHandle quadruped = makeWalkerFromRecipe(world, glm::vec3(6.0f, 0.0f, 9.0f), "quadruped length=1.0 speed=1.4", kit.sphere, kit.cube);
    paint(world, quadruped, quadPaint);
    setWalkerTarget(world, quadruped, glm::vec3(12.0f, 0.0f, 9.0f));

    const EntityHandle biped = makeWalkerFromRecipe(world, glm::vec3(0.0f, 0.0f, -2.0f), "biped length=1.0 speed=1.1", kit.sphere, kit.cube);
    paint(world, biped, bipedPaint);
    const EntityHandle goal = ikTestPushableSphere(world, kit, kIkTestBipedGoalName, goalPaint, glm::vec3(-2.0f, 0.3f, -5.0f), 0.3f);
    setWalkerTarget(world, biped, world.getWorldPosition(goal));
}

}

void buildIkTestRigs(World& world, const IkTestKit& kit) {
    buildArm(world, kit);
    buildLookHead(world, kit);
    buildTentacle(world, kit);
    buildCcdChain(world, kit);
    buildWalkers(world, kit);
}
