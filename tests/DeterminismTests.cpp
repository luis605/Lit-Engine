#include "TestSupport.hpp"
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Audio;
import Engine.Geometry;
import Engine.Json;
import Engine.Nav;
import Engine.Params;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Replay;
import Engine.Rig;
import Engine.Rng;
import Engine.Save;
import Engine.State;
import Engine.Storylet;
import Engine.World;
import Engine.WorldHash;
import Engine.glm;

namespace {
constexpr float kStep = 1.0f / 60.0f;
constexpr int kTicks = 360;
constexpr int kHashEvery = 30;
constexpr int kSavePoint = 150;
constexpr int kSphereCount = 24;
constexpr uint32_t kSphereMesh = 1;

const char* kSchema = R"({"vars": {"bond": {"type": "range", "min": 0, "max": 1, "default": 0.25}, "drift": {"type": "range", "min": 0, "max": 1},
  "attends": {"type": "counter", "min": 0, "max": 1000000}, "location_tag": {"type": "set"}}})";

const char* kStorylets = R"({"storylets": [
  {"id": "hello", "when": "bond >= 0.2", "priority": 10, "cooldown_seconds": 2, "lines": [{"text_id": "hello_a", "seconds": 1}], "effects": ["attends += 1"]},
  {"id": "tie_a", "when": "attends >= 3", "priority": 7, "cooldown_seconds": 3, "lines": [{"text_id": "ta", "seconds": 0.2}], "effects": ["bond += 0.01"]},
  {"id": "tie_b", "when": "attends >= 3", "priority": 7, "cooldown_seconds": 3, "lines": [{"text_id": "tb", "seconds": 0.2}]}]})";

const char* kAgentGraph = R"({"name": "agent", "blackboard": {"hunger": 0.2, "fatigue": 0.1, "drive": 0.3},
  "root": {"type": "utility", "name": "needs", "stickiness": 0.05, "select": "weighted", "options": [
    {"name": "eat", "considerations": [{"input": "hunger", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "eat"}},
    {"name": "rest", "considerations": [{"input": "fatigue", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "rest"}},
    {"name": "work", "considerations": [{"input": "drive"}], "node": {"type": "action", "action": "work"}}]}})";

void addPlane(World& world) {
    const EntityHandle plane = world.create("floor", 0, glm::vec3(0.0f));
    world.add<PlaneCollider>(plane, glm::vec3(0.0f, 1.0f, 0.0f));
}

void addSpheres(World& world) {
    for (int index = 0; index < kSphereCount; ++index) {
        const float column = static_cast<float>(index % 6);
        const float row = static_cast<float>(index / 6);
        const EntityHandle ball = world.create("ball" + std::to_string(index), kSphereMesh, glm::vec3(column * 0.55f, 1.0f + row * 0.6f + column * 0.05f, row * 0.1f));
        world.add<SphereCollider>(ball, 0.3f);
        RigidBody* body = world.add<RigidBody>(ball);
        body->restitution = 0.4f;
        body->velocity = glm::vec3(0.1f * static_cast<float>(index % 3), 0.0f, 0.0f);
    }
}

Skeleton legSkeleton() {
    Skeleton skeleton;
    skeleton.add("pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    const int thigh = skeleton.add("thigh", 0, glm::vec3(0.1f, 0.0f, 0.0f));
    const int shin = skeleton.add("shin", thigh, glm::vec3(0.0f, -0.5f, 0.0f));
    skeleton.add("foot", shin, glm::vec3(0.0f, -0.5f, 0.0f));
    return skeleton;
}

uint64_t hashPose(const Pose& pose) {
    StateHasher hasher;
    for (const JointPose& joint : pose.joints) {
        hasher.addVec3(joint.position.x, joint.position.y, joint.position.z);
        hasher.addFloat(joint.rotation.x);
        hasher.addFloat(joint.rotation.y);
        hasher.addFloat(joint.rotation.z);
        hasher.addFloat(joint.rotation.w);
    }
    return hasher.value();
}

uint64_t hashVoiceEvents(const VoiceScheduler& scheduler) {
    StateHasher hasher;
    for (const LineEventRecord& record : scheduler.events()) {
        hasher.addU64(static_cast<uint64_t>(record.type));
        hasher.addText(record.id);
        hasher.addFloat(record.clipTime);
        hasher.addU64(record.update);
    }
    hasher.addFloat(scheduler.energy());
    return hasher.value();
}

struct FullSim {
    World world;
    StateStore state{&world.events()};
    ParamBus params;
    std::unique_ptr<StoryletEngine> story;
    BehaviorGraph graph;
    ActionRegistry actions;
    std::unique_ptr<BehaviorRunner> runner;
    NavMesh nav;
    NavAgent agent;
    AudioMixer mixer;
    std::unique_ptr<VoiceScheduler> voices;
    Skeleton skeleton = legSkeleton();
    Pose pose = Pose::bind(skeleton);
    std::vector<float> audioBlock = std::vector<float>(512 * 2);
    int tick = 0;

    explicit FullSim(uint64_t seed) {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        registerPhysicsComponents(world);
        world.random().seed(seed);
        addPlane(world);
        addSpheres(world);
        state.loadSchema(kSchema);
        params.declare("tension", 0.0f, 0.2f, 0.0f, 1.0f);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        story->loadText(kStorylets);
        graph.loadText(kAgentGraph);
        registerActions();
        runner = std::make_unique<BehaviorRunner>(graph, actions, world.random().stream("ai.agent"));
        TriangleSoup soup;
        soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(10.0f, 0.5f, 10.0f));
        soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -2.0f)), glm::vec3(0.25f, 1.0f, 5.0f));
        nav.setGeometry(soup);
        nav.rebuildAll();
        agent.position = glm::vec3(-6.0f, 0.0f, 0.0f);
        navSetTarget(nav, agent, glm::vec3(6.0f, 0.0f, 0.0f));
        voices = std::make_unique<VoiceScheduler>(mixer);
        mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        enqueueLine("a", 300.0f, 1.5f, 0);
        enqueueLine("b", 450.0f, 0.5f, 3);
    }

    void registerActions() {
        actions.add("eat", [](AiContext& context) {
            context.blackboard.setNumber("hunger", context.blackboard.number("hunger") * 0.5);
            return Status::Success;
        });
        actions.add("rest", [](AiContext& context) {
            context.blackboard.setNumber("fatigue", context.blackboard.number("fatigue") * 0.5);
            return Status::Success;
        });
        actions.add("work", [](AiContext& context) {
            context.blackboard.setNumber("hunger", context.blackboard.number("hunger") + 0.07);
            context.blackboard.setNumber("fatigue", context.blackboard.number("fatigue") + 0.05);
            return Status::Success;
        });
    }

    void enqueueLine(const std::string& id, float frequency, float seconds, int priority) {
        VoiceLine line;
        line.id = id;
        line.priority = priority;
        line.clip = AudioClip::sine(id, frequency, seconds);
        line.position = glm::vec3(0.0f, 0.0f, 4.0f);
        voices->enqueue(line);
    }

    void step() {
        PhysicsSettings settings;
        stepPhysics(world, kStep, settings);
        auto& drift = world.random().stream("drift");
        state.add("drift", drift.range(0.0f, 0.004f));
        if (tick % 90 == 0) state.add("attends", 1);
        params.set("tension", static_cast<float>(state.get("drift")));
        params.update(kStep);
        story->update(kStep);
        runner->tick(kStep);
        navStepAgent(nav, agent, kStep);
        mixer.render(audioBlock.data(), 512);
        voices->update(512.0f / 48000.0f);
        const glm::vec3 target(0.1f, -0.1f * static_cast<float>(tick % 10), 0.2f);
        solveTwoBone(skeleton, pose, TwoBoneChain{1, 2, 3}, target);
        world.timeState().frame++;
        world.timeState().elapsed += kStep;
        ++tick;
    }

    uint64_t hash() const {
        HashSources sources;
        sources.state = &state;
        sources.storylets = story.get();
        sources.params = &params;
        sources.nav = &nav;
        StateHasher agentHasher;
        agentHasher.addVec3(agent.position.x, agent.position.y, agent.position.z);
        agentHasher.addVec3(agent.velocity.x, agent.velocity.y, agent.velocity.z);
        agentHasher.addU64(agent.repaths);
        sources.extra = {runner->stateHash(), agentHasher.value(), hashVoiceEvents(*voices), hashPose(pose)};
        return hashWorldState(world, sources);
    }
};

std::vector<ReplayCheckpoint> runFull(uint64_t seed) {
    FullSim sim(seed);
    std::vector<ReplayCheckpoint> hashes;
    for (int index = 0; index <= kTicks; ++index) {
        if (index % kHashEvery == 0) hashes.push_back({static_cast<uint64_t>(index), sim.hash()});
        if (index < kTicks) sim.step();
    }
    return hashes;
}

void testFullSimulationRepeats() {
    const std::vector<ReplayCheckpoint> first = runFull(5);
    const std::vector<ReplayCheckpoint> second = runFull(5);
    const std::vector<ReplayCheckpoint> third = runFull(5);
    CHECK(first.size() == static_cast<size_t>(kTicks / kHashEvery + 1));
    CHECK(first == second);
    CHECK(first == third);
    CHECK(first != runFull(6));
    CHECK(first.front().hash != first.back().hash);
}

struct SaveSim {
    World world;
    StateStore state{&world.events()};
    ParamBus params;
    std::unique_ptr<StoryletEngine> story;
    std::unique_ptr<SaveSystem> save;
    int tick = 0;

    explicit SaveSim(uint64_t seed) {
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        registerPhysicsComponents(world);
        world.random().seed(seed);
        state.loadSchema(kSchema);
        params.declare("tension", 0.0f, 0.2f, 0.0f, 1.0f);
        story = std::make_unique<StoryletEngine>(state, world.random(), &world.events());
        story->loadText(kStorylets);
        save = std::make_unique<SaveSystem>(SaveContext{&world, &state, story.get(), &params});
    }

    void populate() {
        addPlane(world);
        addSpheres(world);
    }

    void step() {
        stepPhysics(world, kStep, PhysicsSettings{});
        auto& drift = world.random().stream("drift");
        state.add("drift", drift.range(0.0f, 0.004f));
        if (tick % 90 == 0) state.add("attends", 1);
        params.set("tension", static_cast<float>(state.get("drift")));
        params.update(kStep);
        story->update(kStep);
        world.timeState().frame++;
        world.timeState().elapsed += kStep;
        ++tick;
    }

    uint64_t hash() const {
        HashSources sources;
        sources.state = &state;
        sources.storylets = story.get();
        sources.params = &params;
        return hashWorldState(world, sources);
    }
};

void testSaveRestoreRepeats() {
    SaveSim reference(9);
    reference.populate();
    std::vector<ReplayCheckpoint> expected;
    Json saved;
    for (int index = 0; index <= kTicks; ++index) {
        if (index == kSavePoint) saved = reference.save->capture("point");
        if (index >= kSavePoint && index % kHashEvery == 0) expected.push_back({static_cast<uint64_t>(index), reference.hash()});
        if (index < kTicks) reference.step();
    }
    CHECK(!expected.empty());
    for (int attempt = 0; attempt < 3; ++attempt) {
        SaveSim restored(1234 + static_cast<uint64_t>(attempt));
        std::string error;
        CHECK_OK((restored.save->apply(saved, &error) ? std::string() : "apply failed: " + error));
        restored.tick = kSavePoint;
        std::vector<ReplayCheckpoint> actual;
        for (int index = kSavePoint; index <= kTicks; ++index) {
            if (index % kHashEvery == 0) actual.push_back({static_cast<uint64_t>(index), restored.hash()});
            if (index < kTicks) restored.step();
        }
        CHECK(actual == expected);
    }
}

void testHashSensitivity() {
    SaveSim sim(3);
    sim.populate();
    const uint64_t before = sim.hash();
    CHECK(before == sim.hash());
    sim.world.setPosition(sim.world.findAll("ball0").front(), glm::vec3(0.0f, 5.0f, 0.0f));
    const uint64_t moved = sim.hash();
    CHECK(moved != before);
    sim.world.setPosition(sim.world.findAll("ball0").front(), glm::vec3(0.0f, 5.00001f, 0.0f));
    CHECK(sim.hash() == moved);
    sim.world.random().stream("drift").next();
    CHECK(sim.hash() != moved);
    CHECK(quantiseForHash(0.00004f) == 0 && quantiseForHash(0.0001f) == 1);
}

void testQueryOrderIsIndexOrder() {
    World world;
    world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
    std::vector<EntityHandle> created;
    for (int index = 0; index < 20; ++index) {
        created.push_back(world.create("e" + std::to_string(index), 1, glm::vec3(static_cast<float>((index * 7) % 5), 0.0f, 0.0f)));
        world.setTag(created.back(), "tagged");
    }
    for (int index = 0; index < 20; index += 3) world.setTag(created[static_cast<size_t>(index)], "other");
    for (int index = 0; index < 20; index += 3) world.setTag(created[static_cast<size_t>(index)], "tagged");
    const std::vector<EntityHandle> tagged = world.findByTag("tagged");
    const std::vector<EntityHandle> near = world.overlapSphere(glm::vec3(0.0f), 10.0f);
    bool sortedTags = true;
    for (size_t position = 1; position < tagged.size(); ++position) sortedTags = sortedTags && tagged[position - 1].index < tagged[position].index;
    bool sortedNear = true;
    for (size_t position = 1; position < near.size(); ++position) sortedNear = sortedNear && near[position - 1].index < near[position].index;
    CHECK(tagged.size() == 20 && sortedTags);
    CHECK(near.size() == 20 && sortedNear);
}

void testReplayCheckpointsRoundTrip() {
    ReplayData data;
    data.seed = 42;
    data.ticks.assign(10, {});
    data.hashInterval = 5;
    data.checkpoints = {{0, 0x0123456789abcdefULL}, {5, 0xfedcba9876543210ULL}, {10, 7}};
    ReplayData parsed;
    std::string error;
    CHECK(parseReplay(serializeReplay(data), parsed, &error));
    CHECK(parsed.hashInterval == 5 && parsed.checkpoints == data.checkpoints);

    ReplayVerifier verifier;
    verifier.begin(parsed);
    CHECK(verifier.due(5) && !verifier.due(6));
    verifier.check(0, 0x0123456789abcdefULL);
    verifier.check(5, 1);
    verifier.check(10, 7);
    CHECK(!verifier.ok() && verifier.checked() == 3 && verifier.mismatches().size() == 1);
    CHECK(verifier.mismatches().front().tick == 5 && verifier.mismatches().front().actual == 1);
}
}

int main() {
    testReplayCheckpointsRoundTrip();
    testHashSensitivity();
    testQueryOrderIsIndexOrder();
    testFullSimulationRepeats();
    testSaveRestoreRepeats();
    return testsupport::finish();
}
