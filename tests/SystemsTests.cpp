#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

import Engine.AI;
import Engine.Nav;
import Engine.Rig;
import Engine.Face;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.Geometry;
import Engine.World;
import Engine.Physics;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Params;
import Engine.glm;
import Engine.Json;
import Engine.Rng;

namespace {
int g_failures = 0;
int g_checks = 0;

void check(bool ok, const char* expr, int line) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    }
}
}

#define CHECK(x) check(static_cast<bool>(x), #x, __LINE__)

static std::string tempPath(const std::string& name) { return (std::filesystem::temp_directory_path() / ("lit_systems_" + name)).string(); }

static bool near(float a, float b, float eps = 1.0e-3f) { return std::fabs(a - b) <= eps; }

static const char* kAgentGraph = R"({
  "name": "agent",
  "blackboard": {"hunger": 0.2, "fatigue": 0.1, "drive": 0.3, "alarm": 0.0},
  "root": {"type": "selector", "name": "top", "children": [
    {"type": "sequence", "name": "panic", "children": [
      {"type": "condition", "when": {"key": "alarm", "op": ">", "value": 0.5}},
      {"type": "action", "action": "flee"}]},
    {"type": "utility", "name": "needs", "stickiness": 0.05, "SELECT", "options": [
      {"name": "eat", "considerations": [{"input": "hunger", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "eat"}},
      {"name": "rest", "considerations": [{"input": "fatigue", "curve": [[0, 0], [1, 1]]}], "node": {"type": "action", "action": "rest"}},
      {"name": "work", "considerations": [{"input": "drive"}], "node": {"type": "action", "action": "work"}}]}
  ]}
})";

static std::string agentGraph(bool weighted) {
    std::string t = kAgentGraph;
    const std::string key = "\"SELECT\"";
    t.replace(t.find(key), key.size(), weighted ? "\"select\": \"weighted\"" : "\"select\": \"best\"");
    return t;
}

static void registerAgentActions(ActionRegistry& actions, int* counts) {
    actions.add("flee", [counts](AiContext&) {
        ++counts[3];
        return Status::Success;
    });
    actions.add("eat", [counts](AiContext& c) {
        ++counts[0];
        c.blackboard.setNumber("hunger", std::max(0.0, c.blackboard.number("hunger") - 0.5));
        return Status::Success;
    });
    actions.add("rest", [counts](AiContext& c) {
        ++counts[1];
        c.blackboard.setNumber("fatigue", std::max(0.0, c.blackboard.number("fatigue") - 0.5));
        return Status::Success;
    });
    actions.add("work", [counts](AiContext& c) {
        ++counts[2];
        c.blackboard.setNumber("hunger", c.blackboard.number("hunger") + 0.07);
        c.blackboard.setNumber("fatigue", c.blackboard.number("fatigue") + 0.05);
        return Status::Success;
    });
}

struct AgentRun {
    uint64_t hash = 0;
    std::string trace;
    int counts[4]{};
};

static AgentRun runAgent(bool weighted, uint64_t seed) {
    BehaviorGraph graph;
    std::string error;
    CHECK(graph.loadText(agentGraph(weighted), &error));
    AgentRun run;
    ActionRegistry actions;
    registerAgentActions(actions, run.counts);
    RandomService random;
    random.seed(seed);
    BehaviorRunner runner(graph, actions, random.stream("ai.agent"));
    for (int i = 0; i < 300; ++i) {
        if (i == 150) runner.blackboard().setNumber("alarm", 1.0);
        if (i == 160) runner.blackboard().setNumber("alarm", 0.0);
        runner.tick(1.0f / 30.0f);
    }
    run.hash = runner.stateHash();
    run.trace = runner.trace().toJson().dump();
    return run;
}

static void testAi() {
    const AgentRun a = runAgent(false, 7);
    const AgentRun b = runAgent(false, 7);
    CHECK(a.hash == b.hash);
    CHECK(a.trace == b.trace);
    CHECK(a.counts[0] > 0 && a.counts[1] > 0 && a.counts[2] > 0);
    CHECK(a.counts[3] == 10);

    const AgentRun w1 = runAgent(true, 1);
    const AgentRun w1b = runAgent(true, 1);
    const AgentRun w2 = runAgent(true, 2);
    CHECK(w1.hash == w1b.hash && w1.trace == w1b.trace);
    CHECK(w1.trace != w2.trace);

    BehaviorGraph graph;
    CHECK(graph.loadText(agentGraph(false)));
    int counts[4]{};
    ActionRegistry actions;
    registerAgentActions(actions, counts);
    Pcg32 rng(1);
    BehaviorRunner runner(graph, actions, rng);
    runner.blackboard().setNumber("hunger", 0.9);
    runner.blackboard().setNumber("fatigue", 0.1);
    runner.tick(0.1f);
    const auto events = runner.trace().forTick(0);
    const TraceEvent* needs = nullptr;
    for (const TraceEvent* e : events) {
        if (e->node == "needs") needs = e;
    }
    CHECK(needs != nullptr);
    if (needs) {
        CHECK(needs->winner == "eat");
        CHECK(needs->options.size() == 3);
        CHECK(needs->reason.find("highest score") != std::string::npos);
        CHECK(needs->reason.find("'work'") != std::string::npos || needs->reason.find("'rest'") != std::string::npos);
        CHECK(needs->options[0].considerations.size() == 1 && near(static_cast<float>(needs->options[0].considerations[0].value), 0.9f));
        CHECK(needs->options[0].score > needs->options[1].score);
    }
    const std::string path = tempPath("trace.json");
    CHECK(runner.trace().writeFile(path));
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto parsed = Json::parse(ss.str());
    CHECK(parsed && parsed->get("events") && parsed->get("events")->size() == runner.trace().events().size());
    CHECK(runner.trace().writeFile(path, true));
    std::filesystem::remove(path);
    CHECK(runner.trace().toText().find("eat") != std::string::npos);

    BehaviorGraph bad;
    std::string error;
    CHECK(!bad.loadText(R"({"root":{"type":"teleport"}})", &error));
    CHECK(error.find("unknown node type") != std::string::npos);
    CHECK(!bad.loadText(R"({"root":{"type":"utility","options":[{"name":"a","node":{"type":"wait"},"considerations":[{"input":"x","curve":[[1,0],[0,1]]}]}]}})", &error));

    BehaviorGraph fsm;
    CHECK(fsm.loadText(R"({"root":{"type":"fsm","name":"mode","initial":"idle","states":{
      "idle":{"node":{"type":"wait","seconds":100},"transitions":[{"to":"work","when":{"key":"job","op":">","value":0}}]},
      "work":{"node":{"type":"action","action":"work"},"transitions":[{"to":"rest","after":0.35},{"to":"idle","when":{"key":"job","op":"==","value":0}}]},
      "rest":{"node":{"type":"wait","seconds":100},"transitions":[{"to":"$success","after":0.2}]}}}})"));
    Pcg32 rng2(1);
    int c2[4]{};
    ActionRegistry a2;
    registerAgentActions(a2, c2);
    BehaviorRunner fr(fsm, a2, rng2);
    fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "idle");
    fr.blackboard().setNumber("job", 1);
    fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "work");
    for (int i = 0; i < 4; ++i) fr.tick(0.1f);
    CHECK(fr.stateOf("mode") == "rest");
    Status last = Status::Running;
    for (int i = 0; i < 4 && last == Status::Running; ++i) last = fr.tick(0.1f);
    CHECK(last == Status::Success);
    const auto& evs = fr.trace().events();
    CHECK(std::any_of(evs.begin(), evs.end(), [](const TraceEvent& e) { return e.kind == "fsm" && e.winner == "work" && e.reason.find("job > 0") != std::string::npos; }));

    BehaviorGraph basic;
    CHECK(basic.loadText(R"({"root":{"type":"sequence","children":[{"type":"wait","seconds":0.45},{"type":"set","key":"done","value":true},{"type":"inverter","child":{"type":"condition","when":{"key":"missing"}}}]}})"));
    Pcg32 rng3(1);
    ActionRegistry none;
    BehaviorRunner br(basic, none, rng3);
    int running = 0;
    Status s = Status::Running;
    while (s == Status::Running && running < 20) {
        s = br.tick(0.1f);
        if (s == Status::Running) ++running;
    }
    CHECK(running == 4 && s == Status::Success && br.blackboard().boolean("done"));
}

static TriangleSoup floorWithWall() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(20.0f, 0.5f, 20.0f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, -5.0f)), glm::vec3(0.25f, 1.0f, 15.0f));
    return soup;
}

static void testGeometry() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f)), glm::vec3(1.0f));
    Bvh bvh;
    bvh.build(soup);
    const auto hit = bvh.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(hit && near(hit->t, 4.0f) && near(hit->normal.z, -1.0f));
    CHECK(!bvh.raycast(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    CHECK(bvh.occluded(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 9.0f)));
    CHECK(!bvh.occluded(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 3.0f)));
    std::vector<uint32_t> tris;
    Aabb q;
    q.grow(glm::vec3(-0.1f, -0.1f, 3.9f));
    q.grow(glm::vec3(0.1f, 0.1f, 4.1f));
    bvh.query(q, tris);
    CHECK(!tris.empty());

    World world;
    const auto floor = world.create("floor", NO_MESH, glm::vec3(0.0f, -0.5f, 0.0f));
    world.add<BoxCollider>(floor, glm::vec3(10.0f, 0.5f, 10.0f));
    TriangleSoup fromWorld;
    collectColliderGeometry(world, fromWorld);
    CHECK(fromWorld.triangleCount() == 12);
    CHECK(near(fromWorld.bounds().hi.y, 0.0f));
}

static float pathLength(const NavPath& p) { return p.length; }

static void testNav() {
    NavMesh mesh;
    mesh.setGeometry(floorWithWall());
    mesh.rebuildAll();
    CHECK(mesh.tileCount() > 4 && mesh.walkableCount() > 1000);
    const glm::vec3 from(-5.0f, 0.0f, 0.0f);
    const glm::vec3 to(5.0f, 0.0f, 0.0f);
    const NavPath direct = mesh.findPath(from, to);
    CHECK(direct.found && !direct.partial);
    CHECK(pathLength(direct) > 22.0f && pathLength(direct) < 45.0f);
    bool crosses = false;
    for (size_t i = 1; i < direct.points.size(); ++i) {
        const glm::vec3 a = direct.points[i - 1].position;
        const glm::vec3 b = direct.points[i].position;
        if (a.x * b.x < 0.0f && std::max(a.z, b.z) < 9.5f) crosses = true;
    }
    CHECK(!crosses);

    NavMesh again;
    again.setGeometry(floorWithWall());
    again.rebuildAll();
    CHECK(again.hash() == mesh.hash());

    const uint32_t blocker = mesh.addObstacle([] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    CHECK(mesh.dirtyCount() > 0 && mesh.dirtyCount() < mesh.tileCount());
    const uint64_t versionBefore = mesh.version();
    const size_t dirtyBefore = mesh.dirtyCount();
    CHECK(mesh.rebuildDirty(1) == 1);
    CHECK(mesh.dirtyCount() == dirtyBefore - 1);
    mesh.rebuildDirty();
    CHECK(mesh.version() != versionBefore);
    const NavPath blocked = mesh.findPath(from, to);
    CHECK(!blocked.found || blocked.partial);
    CHECK(mesh.removeObstacle(blocker));
    mesh.rebuildDirty();
    const NavPath restored = mesh.findPath(from, to);
    CHECK(restored.found && !restored.partial && near(restored.length, direct.length, 0.01f));
    CHECK(mesh.hash() == again.hash());

    const uint32_t shortcut = mesh.addObstacle([] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    mesh.rebuildDirty();
    mesh.moveObstacle(shortcut, [] {
        Aabb b;
        b.grow(glm::vec3(-0.5f, 0.0f, 15.0f));
        b.grow(glm::vec3(0.5f, 2.0f, 20.5f));
        return b;
    }());
    mesh.rebuildDirty();
    const NavPath moved = mesh.findPath(from, to);
    CHECK(moved.found && !moved.partial && near(moved.length, direct.length, 0.5f));

    World world;
    const auto floor = world.create("floor", NO_MESH, glm::vec3(0.0f, -0.5f, 0.0f));
    world.add<BoxCollider>(floor, glm::vec3(10.0f, 0.5f, 10.0f));
    NavMesh wm;
    wm.buildFromWorld(world);
    const NavPath wp = wm.findPath(glm::vec3(-6.0f, 0.0f, -6.0f), glm::vec3(6.0f, 0.0f, 6.0f));
    CHECK(wp.found && !wp.partial && near(wp.length, std::sqrt(2.0f) * 12.0f, 0.5f));
}

static void testNavLinksAndAgents() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, -0.5f, 0.0f)), glm::vec3(2.0f, 0.5f, 2.0f));
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, -0.5f, 0.0f)), glm::vec3(2.0f, 0.5f, 2.0f));
    NavMesh mesh;
    mesh.setGeometry(soup);
    mesh.rebuildAll();
    const glm::vec3 from(-3.5f, 0.0f, 0.0f);
    const glm::vec3 to(3.5f, 0.0f, 0.0f);
    const NavPath without = mesh.findPath(from, to);
    CHECK(!without.found || without.partial);

    NavLink link;
    link.a = glm::vec3(-1.8f, 0.0f, 0.0f);
    link.b = glm::vec3(1.8f, 0.0f, 0.0f);
    link.userData = 42;
    const uint32_t id = mesh.addLink(link);
    const NavPath with = mesh.findPath(from, to);
    CHECK(with.found && !with.partial);
    int linkPoints = 0;
    for (const NavPathPoint& p : with.points) linkPoints += p.link == static_cast<int>(id) ? 1 : 0;
    CHECK(linkPoints == 1);

    mesh.setLinkEnabled(id, false);
    const NavPath off = mesh.findPath(from, to);
    CHECK(!off.found || off.partial);
    mesh.setLinkEnabled(id, true);

    NavAgent agent;
    agent.position = from;
    navSetTarget(mesh, agent, to);
    CHECK(agent.state == AgentState::Moving);
    int traversals = 0;
    int userData = 0;
    for (int i = 0; i < 900 && agent.state == AgentState::Moving; ++i) {
        navStepAgent(mesh, agent, 1.0f / 30.0f, nullptr, [&](NavAgent& a, const NavLink& l, const glm::vec3& exit) {
            ++traversals;
            userData = l.userData;
            a.position = exit;
        });
    }
    CHECK(agent.state == AgentState::Arrived);
    CHECK(traversals == 1 && userData == 42);
    CHECK(glm::length(agent.position - to) < 0.3f);

    NavAgent crowdA;
    NavAgent crowdB;
    crowdA.position = glm::vec3(-3.5f, 0.0f, -1.0f);
    crowdB.position = glm::vec3(-3.5f, 0.0f, -0.9f);
    navSetTarget(mesh, crowdA, glm::vec3(-3.5f, 0.0f, 1.0f));
    navSetTarget(mesh, crowdB, glm::vec3(-3.5f, 0.0f, 1.0f));
    std::vector<NavAgent> crowd{crowdA, crowdB};
    for (int i = 0; i < 300; ++i) navStepAgents(mesh, crowd, 1.0f / 30.0f);
    CHECK(glm::length(crowd[0].position - glm::vec3(-3.5f, 0.0f, 1.0f)) < 1.0f && glm::length(crowd[1].position - glm::vec3(-3.5f, 0.0f, 1.0f)) < 1.0f);

    NavMesh floorMesh;
    floorMesh.setGeometry(floorWithWall());
    floorMesh.rebuildAll();
    NavAgent walker;
    walker.position = glm::vec3(-5.0f, 0.0f, 0.0f);
    navSetTarget(floorMesh, walker, glm::vec3(5.0f, 0.0f, 0.0f));
    for (int i = 0; i < 60; ++i) navStepAgent(floorMesh, walker, 1.0f / 30.0f);
    const uint32_t repathsBefore = walker.repaths;
    Aabb gap;
    gap.grow(glm::vec3(-0.5f, 0.0f, 9.5f));
    gap.grow(glm::vec3(0.5f, 2.0f, 20.5f));
    floorMesh.addObstacle(gap);
    floorMesh.rebuildDirty();
    for (int i = 0; i < 5; ++i) navStepAgent(floorMesh, walker, 1.0f / 30.0f);
    CHECK(walker.repaths > repathsBefore);
}

static Skeleton legSkeleton() {
    Skeleton sk;
    sk.add("pelvis", -1, glm::vec3(0.0f, 1.0f, 0.0f));
    const int tl = sk.add("thighL", 0, glm::vec3(0.1f, 0.0f, 0.0f));
    const int sl = sk.add("shinL", tl, glm::vec3(0.0f, -0.5f, 0.0f));
    sk.add("footL", sl, glm::vec3(0.0f, -0.5f, 0.0f));
    const int tr = sk.add("thighR", 0, glm::vec3(-0.1f, 0.0f, 0.0f));
    const int sr = sk.add("shinR", tr, glm::vec3(0.0f, -0.5f, 0.0f));
    sk.add("footR", sr, glm::vec3(0.0f, -0.5f, 0.0f));
    const int spine = sk.add("spine", 0, glm::vec3(0.0f, 0.5f, 0.0f));
    const int neck = sk.add("neck", spine, glm::vec3(0.0f, 0.3f, 0.0f));
    sk.add("head", neck, glm::vec3(0.0f, 0.1f, 0.0f));
    return sk;
}

static void testRig() {
    const Skeleton sk = legSkeleton();
    CHECK(sk.find("footR") == 6 && sk.find("nope") == -1);
    Pose bind = Pose::bind(sk);
    const ModelPose mp = computeModelPose(sk, bind);
    CHECK(near(mp.position[3].y, 0.0f) && near(mp.position[3].x, 0.1f));

    Pose pose = bind;
    const TwoBoneChain left{1, 2, 3};
    const glm::vec3 target(0.1f, 0.2f, 0.3f);
    CHECK(solveTwoBone(sk, pose, left, target, glm::vec3(0.1f, 0.5f, 1.0f)));
    ModelPose after = computeModelPose(sk, pose);
    CHECK(glm::length(after.position[3] - target) < 1.0e-3f);
    CHECK(near(glm::length(after.position[2] - after.position[1]), 0.5f, 1.0e-3f));
    CHECK(after.position[2].z > 0.1f);

    Pose far = bind;
    solveTwoBone(sk, far, left, glm::vec3(0.1f, -3.0f, 0.0f));
    after = computeModelPose(sk, far);
    CHECK(near(glm::length(after.position[3] - after.position[1]), 1.0f, 2.0e-3f));

    Pose none = bind;
    solveTwoBone(sk, none, left, target, std::nullopt, 0.0f);
    const ModelPose same = computeModelPose(sk, none);
    CHECK(glm::length(same.position[3] - mp.position[3]) < 1.0e-4f);

    Pose stand = bind;
    const std::vector<FootIkLeg> legs{{TwoBoneChain{1, 2, 3}, 0.0f}, {TwoBoneChain{4, 5, 6}, 0.0f}};
    FootIkSettings settings;
    settings.pelvis = 0;
    const FootIkResult res = solveFootIk(sk, stand, legs, settings, glm::mat4(1.0f), [](const glm::vec3& p) -> std::optional<GroundSample> {
        GroundSample g;
        g.height = p.x > 0.0f ? -0.2f : 0.0f;
        return g;
    });
    CHECK(near(res.pelvisOffset, -0.2f, 1.0e-3f));
    CHECK(res.planted[0] && res.planted[1]);
    CHECK(res.footError[0] < 2.0e-3f && res.footError[1] < 2.0e-3f);
    const ModelPose planted = computeModelPose(sk, stand);
    CHECK(near(planted.position[3].y, -0.2f, 2.0e-3f) && near(planted.position[6].y, 0.0f, 2.0e-3f));

    Pose gaze = bind;
    const std::vector<LookAtJoint> chain{{7, 0.2f}, {8, 0.3f}, {9, 0.5f}};
    const glm::vec3 lookTarget(3.0f, 1.9f, 3.0f);
    solveLookAt(sk, gaze, chain, glm::vec3(0.0f, 0.0f, 1.0f), lookTarget);
    const ModelPose gp = computeModelPose(sk, gaze);
    const glm::vec3 fwd = gp.rotation[9] * glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 want = glm::normalize(lookTarget - gp.position[9]);
    CHECK(glm::dot(fwd, want) > 0.9995f);
    Pose limited = bind;
    solveLookAt(sk, limited, chain, glm::vec3(0.0f, 0.0f, 1.0f), lookTarget, glm::radians(20.0f));
    const ModelPose lp = computeModelPose(sk, limited);
    const glm::vec3 lf = lp.rotation[9] * glm::vec3(0.0f, 0.0f, 1.0f);
    CHECK(near(std::acos(std::clamp(glm::dot(lf, glm::vec3(0.0f, 0.0f, 1.0f)), -1.0f, 1.0f)), glm::radians(20.0f), 0.01f));

    SkeletalLibrary lib;
    SkeletalClip a;
    a.name = "low";
    a.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}, {}, {}});
    SkeletalClip b;
    b.name = "high";
    b.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 3.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 3.0f, 0.0f)}}, {}, {}});
    const uint32_t ia = lib.add(a);
    const uint32_t ib = lib.add(b);
    CHECK(lib.find("high") == static_cast<int>(ib));
    SkeletalAnimator anim(sk, lib);
    anim.play(ia, 0.0f);
    anim.update(0.1f);
    CHECK(near(anim.pose().joints[0].position.y, 1.0f));
    anim.play(ib, 1.0f);
    anim.update(0.5f);
    CHECK(near(anim.pose().joints[0].position.y, 2.0f, 1.0e-3f));
    anim.update(0.6f);
    CHECK(near(anim.pose().joints[0].position.y, 3.0f));

    const std::vector<float> mask = subtreeMask(sk, 1);
    CHECK(mask[1] == 1.0f && mask[2] == 1.0f && mask[3] == 1.0f && mask[0] == 0.0f && mask[4] == 0.0f);
    Pose pa = Pose::bind(sk);
    Pose pb = pa;
    pb.joints[0].position.y = 5.0f;
    pb.joints[2].position.y = -0.1f;
    Pose masked;
    blendPosesMasked(pa, pb, mask, 1.0f, masked);
    CHECK(near(masked.joints[0].position.y, 1.0f) && near(masked.joints[2].position.y, -0.1f));
    Pose half;
    blendPoses(pa, pb, 0.5f, half);
    CHECK(near(half.joints[0].position.y, 3.0f));

    SkeletalClip walkA;
    walkA.name = "slow";
    walkA.duration = 2.0f;
    walkA.tracks.push_back({0, {{0.0f, glm::vec3(0.0f, 1.0f, 0.0f)}, {2.0f, glm::vec3(0.0f, 1.0f, 0.0f)}}, {}, {}});
    SkeletalClip walkB = walkA;
    walkB.name = "fast";
    walkB.duration = 1.0f;
    walkB.tracks[0].position = {{0.0f, glm::vec3(0.0f, 2.0f, 0.0f)}, {1.0f, glm::vec3(0.0f, 2.0f, 0.0f)}};
    const uint32_t ws = lib.add(walkA);
    const uint32_t wf = lib.add(walkB);
    SkeletalAnimator blend(sk, lib);
    blend.playBlendSpace({{0.0f, ws}, {1.0f, wf}}, 0.5f, 0.0f);
    blend.update(0.1f);
    CHECK(near(blend.pose().joints[0].position.y, 1.5f, 1.0e-3f));

    World world;
    std::vector<EntityHandle> joints;
    for (size_t i = 0; i < sk.size(); ++i) joints.push_back(world.create("j" + std::to_string(i)));
    applyPoseToEntities(world, joints, planted.position.empty() ? Pose::bind(sk) : stand);
    CHECK(near(world.getPosition(joints[0]).y, 0.8f, 1.0e-3f));
}

static void testFace() {
    CHECK(kArkitBlendshapes.size() == 52);
    CHECK(blendshapeIndex("eyeBlinkLeft") == 0 && blendshapeIndex("tongueOut") == 51 && blendshapeIndex("jawOpen") == 17 && blendshapeIndex("bogus") == -1);
    std::vector<std::string> names;
    for (const char* n : kArkitBlendshapes) names.emplace_back(n);
    std::sort(names.begin(), names.end());
    CHECK(std::adjacent_find(names.begin(), names.end()) == names.end());

    BlendshapeClip clip;
    std::string error;
    CHECK(clip.loadText(R"({"name":"talk","curves":{"jawOpen":[[0,0],[1,1]],"eyeBlinkLeft":[[0,0],[0.5,1],[1,0]]}})", &error));
    BlendshapeWeights w = blendshapeZero();
    clip.sample(0.5f, w);
    CHECK(near(w[blendshapeIndex("jawOpen")], 0.5f) && near(w[blendshapeIndex("eyeBlinkLeft")], 1.0f));
    clip.sample(5.0f, w);
    CHECK(near(w[blendshapeIndex("jawOpen")], 1.0f));
    BlendshapeClip bad;
    CHECK(!bad.loadText(R"({"curves":{"notAShape":[[0,0]]}})", &error));
    BlendshapeClip fps;
    CHECK(fps.loadText(R"({"name":"f","fps":10,"curves":{"jawOpen":[0,0.5,1]}})"));
    CHECK(near(fps.duration, 0.2f));
    BlendshapeClip round;
    CHECK(round.loadJson(clip.toJson()) && round.curves.size() == clip.curves.size() && near(round.duration, clip.duration));

    BlendshapeLibrary lib;
    const uint32_t id = lib.add(clip);
    BlendshapePlayer player;
    player.play(id);
    BlendshapeWeights out = blendshapeZero();
    player.update(0.5f, lib, out);
    CHECK(player.playing() && near(out[blendshapeIndex("jawOpen")], 0.5f));
    player.update(0.6f, lib, out);
    CHECK(!player.playing() && near(out[blendshapeIndex("jawOpen")], 1.0f));

    EyeSystem eyes;
    Pcg32 rng(5);
    eyes.setHead(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    eyes.setTarget(glm::vec3(10.0f, 0.0f, 10.0f));
    for (int i = 0; i < 30; ++i) eyes.update(1.0f / 60.0f, rng);
    BlendshapeWeights ew = blendshapeZero();
    eyes.applyTo(ew);
    CHECK(ew[blendshapeIndex("eyeLookOutLeft")] > 0.85f && ew[blendshapeIndex("eyeLookInRight")] > 0.85f);
    CHECK(ew[blendshapeIndex("eyeLookInLeft")] < 0.05f && ew[blendshapeIndex("eyeLookUpLeft")] < 0.2f);

    const auto simulate = [](uint64_t seed, float* maxBlink, uint32_t* saccades) {
        EyeSystem e;
        Pcg32 r(seed);
        e.setHead(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        e.setTarget(glm::vec3(0.0f, 0.0f, 5.0f));
        float mx = 0.0f;
        float yawSum = 0.0f;
        for (int i = 0; i < 3600; ++i) {
            e.update(1.0f / 60.0f, r);
            mx = std::max(mx, e.blink());
            yawSum += e.yawDegrees();
        }
        *maxBlink = mx;
        *saccades = e.saccadeCount();
        return e.blinkCount() * 1000.0f + yawSum;
    };
    float m1;
    float m2;
    uint32_t s1;
    uint32_t s2;
    const float r1 = simulate(3, &m1, &s1);
    const float r2 = simulate(3, &m2, &s2);
    CHECK(r1 == r2 && s1 == s2);
    CHECK(m1 > 0.9f && s1 > 15);
    const uint32_t blinks = static_cast<uint32_t>(r1 / 1000.0f);
    CHECK(blinks >= 6 && blinks <= 40);
    float m3;
    uint32_t s3;
    CHECK(simulate(4, &m3, &s3) != r1);
}

static std::vector<float> renderMixer(AudioMixer& mixer, uint32_t frames) {
    std::vector<float> out(static_cast<size_t>(frames) * 2);
    mixer.update();
    mixer.render(out.data(), frames);
    return out;
}

static void energy(const std::vector<float>& buf, float& left, float& right) {
    left = 0.0f;
    right = 0.0f;
    for (size_t i = 0; i + 1 < buf.size(); i += 2) {
        left += buf[i] * buf[i];
        right += buf[i + 1] * buf[i + 1];
    }
}

static float peakOf(const std::vector<float>& buf) {
    float p = 0.0f;
    for (const float f : buf) p = std::max(p, std::fabs(f));
    return p;
}

static void testMixer() {
    const auto tone = AudioClip::sine("tone", 440.0f, 2.0f, 0.5f);
    {
        AudioMixer mixer;
        mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        VoiceParams p;
        p.clip = tone;
        p.position = glm::vec3(-5.0f, 0.0f, 1.0f);
        mixer.play(p);
        float l;
        float r;
        energy(renderMixer(mixer, 8192), l, r);
        CHECK(r > l * 3.0f);

        AudioMixer centered;
        centered.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        centered.setReverbReturn(0.0f);
        p.position = glm::vec3(0.0f, 0.0f, 5.0f);
        centered.play(p);
        energy(renderMixer(centered, 8192), l, r);
        CHECK(std::fabs(l - r) < 0.05f * (l + r));

        AudioMixer far;
        far.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        p.position = glm::vec3(0.0f, 0.0f, 20.0f);
        far.play(p);
        float fl;
        float fr;
        energy(renderMixer(far, 8192), fl, fr);
        CHECK(fl + fr < (l + r) * 0.3f);
    }
    {
        VoiceParams p;
        p.clip = tone;
        p.spatial = false;
        p.bus = "sfx";
        AudioMixer mixer;
        mixer.addBus("sfx");
        mixer.play(p);
        const float full = peakOf(renderMixer(mixer, 2048));
        AudioMixer half;
        half.addBus("sfx");
        half.setBusGainDb("sfx", -6.0206f);
        half.play(p);
        const float h = peakOf(renderMixer(half, 2048));
        CHECK(near(h / full, 0.5f, 0.01f));
        AudioMixer muted;
        muted.addBus("sfx");
        muted.setBusMute("sfx", true);
        muted.play(p);
        CHECK(peakOf(renderMixer(muted, 2048)) == 0.0f);
        CHECK(mixer.busLevel("sfx") > 0.1f);
    }
    {
        AudioMixer mixer;
        mixer.addBus("music");
        ParamBus params;
        params.declare("calm", 0.0f);
        mixer.bindBusParam("music", "calm", 0.0f, 1.0f);
        params.snap("calm", 0.25f);
        mixer.update(&params);
        CHECK(near(mixer.busGain("music"), 0.25f));
        params.snap("calm", 1.0f);
        mixer.update(&params);
        CHECK(near(mixer.busGain("music"), 1.0f));
    }
    {
        AudioMixer mixer;
        mixer.addBus("voice");
        mixer.addBus("music");
        mixer.addDuck("voice", "music", -12.0f, 0.01f, 0.01f, 0.2f);
        VoiceParams music;
        music.clip = AudioClip::sine("m", 220.0f, 1.0f, 0.3f);
        music.spatial = false;
        music.bus = "music";
        music.loop = true;
        mixer.play(music);
        VoiceParams voice;
        voice.clip = AudioClip::sine("v", 880.0f, 0.5f, 0.5f);
        voice.spatial = false;
        voice.bus = "voice";
        mixer.play(voice);
        renderMixer(mixer, 19200);
        CHECK(near(mixer.busDuck("music"), 0.251f, 0.05f));
        renderMixer(mixer, 48000 * 2);
        CHECK(mixer.busDuck("music") > 0.95f);
    }
    {
        const auto run = [&](float* peak) {
            AudioMixer mixer;
            mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
            VoiceParams p;
            p.clip = AudioClip::noise("n", 1.0f, 9, 0.4f);
            p.position = glm::vec3(-3.0f, 0.0f, 2.0f);
            mixer.play(p);
            NullAudioBackend backend;
            renderAudio(mixer, backend, 1.0f);
            *peak = backend.peak();
            return backend.hash();
        };
        float p1;
        float p2;
        CHECK(run(&p1) == run(&p2) && p1 == p2 && p1 > 0.01f);
    }
    {
        const std::string path = tempPath("render.wav");
        AudioMixer mixer;
        VoiceParams p;
        p.clip = tone;
        p.spatial = false;
        mixer.play(p);
        WavAudioBackend wav(path);
        renderAudio(mixer, wav, 1.0f);
        const auto loaded = AudioClip::loadWav(path);
        CHECK(loaded && loaded->sampleRate == 48000 && loaded->samples.size() == 48000);
        if (loaded) {
            float pk = 0.0f;
            for (const float f : loaded->samples) pk = std::max(pk, std::fabs(f));
            CHECK(pk > 0.4f && pk < 0.6f);
        }
        const std::string mono = tempPath("mono.wav");
        CHECK(tone->saveWav(mono));
        const auto back = AudioClip::loadWav(mono);
        CHECK(back && back->samples.size() == tone->samples.size() && near(back->samples[100], tone->samples[100], 1.0e-3f));
        std::filesystem::remove(path);
        std::filesystem::remove(mono);
    }
}

static void testVoiceScheduler() {
    AudioMixer mixer;
    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    VoiceScheduler sched(mixer);
    std::vector<float> buf(512 * 2);
    const auto advance = [&](int blocks) {
        for (int i = 0; i < blocks; ++i) {
            mixer.render(buf.data(), 512);
            sched.update(512.0f / 48000.0f);
        }
    };
    const auto count = [&](LineEvent type, const std::string& id) {
        return std::count_if(sched.events().begin(), sched.events().end(), [&](const LineEventRecord& e) { return e.type == type && e.id == id; });
    };
    VoiceLine line;
    line.id = "a";
    line.clip = AudioClip::sine("a", 300.0f, 2.0f);
    line.position = glm::vec3(0.0f, 0.0f, 5.0f);
    line.maxDistance = 10.0f;
    line.resumeDistance = 8.0f;
    sched.enqueue(line);
    sched.update(0.0f);
    CHECK(sched.current() && sched.current()->id == "a");
    advance(40);
    const float before = sched.clipTime();
    CHECK(before > 0.3f && before < 1.0f);
    CHECK(sched.energy() > 0.1f);

    mixer.setListener(glm::vec3(0.0f, 0.0f, -30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    advance(1);
    CHECK(!sched.current());
    CHECK(count(LineEvent::Interrupted, "a") == 1);
    CHECK(mixer.voiceCount() == 0);
    advance(10);
    CHECK(!sched.current());

    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    advance(1);
    CHECK(sched.current() && count(LineEvent::Resumed, "a") == 1);
    float resumedAt = 0.0f;
    for (const LineEventRecord& e : sched.events()) {
        if (e.type == LineEvent::Resumed) resumedAt = e.clipTime;
    }
    CHECK(resumedAt < before && resumedAt > before - 0.4f);

    VoiceLine urgent;
    urgent.id = "b";
    urgent.priority = 5;
    urgent.interruptible = false;
    urgent.positional = false;
    urgent.clip = AudioClip::sine("b", 500.0f, 0.3f);
    sched.enqueue(urgent);
    CHECK(sched.current() && sched.current()->id == "b");
    CHECK(count(LineEvent::Interrupted, "a") == 2);
    VoiceLine low;
    low.id = "c";
    low.positional = false;
    low.clip = AudioClip::sine("c", 600.0f, 0.2f);
    sched.enqueue(low);
    CHECK(sched.current()->id == "b" && sched.pending() == 2);
    advance(40);
    CHECK(count(LineEvent::Finished, "b") == 1);
    CHECK(count(LineEvent::Resumed, "a") == 2);
    advance(200);
    CHECK(count(LineEvent::Finished, "a") == 1 && count(LineEvent::Finished, "c") == 1);
    CHECK(!sched.current() && sched.pending() == 0);

    VoiceLine fragile;
    fragile.id = "d";
    fragile.resumable = false;
    fragile.positional = false;
    fragile.clip = AudioClip::sine("d", 300.0f, 1.0f);
    sched.enqueue(fragile);
    VoiceLine boss;
    boss.id = "e";
    boss.priority = 9;
    boss.positional = false;
    boss.clip = AudioClip::sine("e", 300.0f, 0.1f);
    sched.enqueue(boss);
    CHECK(count(LineEvent::Dropped, "d") == 1 && sched.current()->id == "e");
}

static void testPropagation() {
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)), glm::vec3(0.2f, 5.0f, 5.0f));
    AudioPropagation prop;
    prop.setScene(soup);
    const glm::vec3 listener(-5.0f, 1.0f, 0.0f);
    prop.beginFrame(listener);
    const AudioPath clear = prop.trace(listener, glm::vec3(-5.0f, 1.0f, 4.0f));
    CHECK(clear.audible && clear.occlusion == 0.0f && near(clear.attenuation, 0.25f, 0.01f));
    const AudioPath blocked = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(blocked.directOcclusion == 1.0f && blocked.attenuation < 0.05f && blocked.lowpassHz < 1000.0f && !blocked.viaHop);

    bool portalOpen = true;
    prop.setHopHook([&](const glm::vec3&, std::vector<AudioHop>& out) {
        if (portalOpen) out.push_back({glm::vec3(-0.5f, 1.0f, 3.0f), glm::vec3(0.5f, 1.0f, 3.0f), 0.0f, 7});
    });
    prop.beginFrame(listener);
    const AudioPath hop = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(hop.viaHop && hop.hopId == 7 && hop.occlusion == 0.0f && hop.attenuation > blocked.attenuation * 2.0f);
    CHECK(near(hop.arrivalDirection.z, 3.0f / std::sqrt(4.5f * 4.5f + 9.0f), 0.01f) && hop.arrivalDirection.x > 0.7f);
    portalOpen = false;
    prop.beginFrame(listener);
    const AudioPath shut = prop.trace(listener, glm::vec3(5.0f, 1.0f, 0.0f));
    CHECK(!shut.viaHop && shut.directOcclusion == 1.0f);

    prop.setHopHook({});
    prop.config().raysPerFrame = 64;
    std::vector<AudioEmitter> emitters;
    for (uint32_t i = 0; i < 20; ++i) emitters.push_back({i + 1, glm::vec3(-5.0f + static_cast<float>(i) * 0.5f, 1.0f, 3.0f)});
    prop.update(listener, emitters);
    CHECK(prop.raysUsed() <= 64 && prop.staleCount() > 0);
    CHECK(prop.result(1) && !prop.result(1)->stale);
    CHECK(prop.result(20) && prop.result(20)->stale);
    prop.config().raysPerFrame = 512;
    prop.update(listener, emitters);
    CHECK(prop.staleCount() == 0 && prop.raysUsed() <= 512);

    TriangleSoup room;
    room.addBox(glm::mat4(1.0f), glm::vec3(5.0f, 3.0f, 5.0f));
    AudioPropagation enclosed;
    enclosed.setScene(room);
    enclosed.beginFrame(glm::vec3(0.0f));
    AudioPropagation open;
    open.beginFrame(glm::vec3(0.0f));
    CHECK(enclosed.reverb().wet > 0.9f && enclosed.reverb().rt60 > 0.2f && enclosed.reverb().rt60 < 3.0f);
    CHECK(open.reverb().wet == 0.0f);

    AudioPropagation reflect;
    TriangleSoup floorSoup;
    floorSoup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(30.0f, 0.5f, 30.0f));
    reflect.setScene(floorSoup);
    reflect.config().reverbRays = 64;
    const glm::vec3 ear(0.0f, 1.5f, 0.0f);
    reflect.beginFrame(ear);
    const AudioPath withBounce = reflect.trace(ear, glm::vec3(4.0f, 1.5f, 0.0f));
    CHECK(!withBounce.reflections.empty() && withBounce.reflections[0].delay > 0.0f && withBounce.reflections[0].gain > 0.0f);

    AudioMixer mixer;
    mixer.setListener(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    VoiceParams p;
    p.clip = AudioClip::noise("n", 1.0f, 3, 0.4f);
    p.position = glm::vec3(0.0f, 0.0f, 5.0f);
    const VoiceId v = mixer.play(p);
    AudioPath viaLeft;
    viaLeft.audible = true;
    viaLeft.attenuation = 0.5f;
    viaLeft.arrivalDirection = glm::vec3(1.0f, 0.0f, 0.0f);
    mixer.setVoicePath(v, viaLeft);
    float l;
    float r;
    energy(renderMixer(mixer, 4096), l, r);
    CHECK(l > r * 3.0f);
}

int main() {
    testAi();
    testGeometry();
    testNav();
    testNavLinksAndAgents();
    testRig();
    testFace();
    testMixer();
    testVoiceScheduler();
    testPropagation();
    std::printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
