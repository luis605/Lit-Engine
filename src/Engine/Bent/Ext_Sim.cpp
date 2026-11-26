#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

import Engine.glm;
import Engine.Geometry;
import Engine.Nav;
import Engine.AI;
import Engine.Json;
import Engine.Rng;
import Engine.Animation;
import Engine.Rig;
import Engine.Skin;
import Engine.SkinRig;
import Engine.Face;
import Engine.IK.Solvers;
import Engine.Audio;
import Engine.AudioTrace;
import Engine.mesh;

#include "Engine/Bent/Ext_PhysicsUtil.hpp"

namespace bent::ext {

namespace {

struct NavBox {
    float half = 20.0f;
};

std::unique_ptr<NavMesh> makeNav(const Rec& spec, std::vector<uint32_t>& ids) {
    const float half = std::clamp(spec.f("half", 20.0f), 1.0f, 200.0f);
    TriangleSoup soup;
    soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(half, 0.5f, half));
    auto mesh = std::make_unique<NavMesh>();
    NavConfig config;
    config.originX = -half;
    config.originZ = -half;
    config.cellSize = std::clamp(spec.f("cell", 0.5f), 0.05f, 10.0f);
    config.agentRadius = std::clamp(spec.f("radius", 0.4f), 0.0f, 10.0f);
    mesh->configure(config);
    mesh->setGeometry(soup);
    for (const Value& ov : spec.list("obstacles")) {
        const Rec o = rec(ov, spec.loc);
        const glm::vec3 c = o.vec("center", glm::vec3(0.0f));
        const glm::vec3 h = glm::abs(o.vec("half", glm::vec3(1.0f)));
        Aabb box;
        box.grow(c - h);
        box.grow(c + h);
        ids.push_back(mesh->addObstacle(box));
    }
    for (const Value& lv : spec.list("links")) {
        const Rec l = rec(lv, spec.loc);
        NavLink link;
        link.a = l.vec("a", glm::vec3(0.0f));
        link.b = l.vec("b", glm::vec3(0.0f));
        link.radius = l.f("radius", 0.6f);
        link.bidirectional = l.flag("bidirectional", true);
        link.cost = l.f("cost", 0.0f);
        mesh->addLink(link);
    }
    mesh->rebuildAll();
    return mesh;
}

Value pathValue(const NavPath& path) {
    ValueList points;
    ValueList links;
    for (const NavPathPoint& p : path.points) {
        points.push_back(vecV(p.position));
        links.push_back(intV(p.link));
    }
    return recV({{"found", boolV(path.found)}, {"partial", boolV(path.partial)}, {"points", listV(std::move(points))}, {"links", listV(std::move(links))}, {"length", numV(path.length)}, {"gridCost", numV(path.gridCost)}, {"expansions", intV(path.expansions)}});
}

Value navEval(const Value& specValue, const Value& opsValue, const SrcLoc& loc) {
    const Rec spec = rec(specValue, loc);
    std::vector<uint32_t> ids;
    std::unique_ptr<NavMesh> mesh = makeNav(spec, ids);
    ValueList out;
    for (const Value& opv : opsValue.items()) {
        const Rec op = rec(opv, loc);
        const std::string kind = op.str("op", "path");
        if (kind == "path") out.push_back(pathValue(mesh->findPath(op.vec("from", glm::vec3(0.0f)), op.vec("to", glm::vec3(0.0f)))));
        else if (kind == "walkable") out.push_back(boolV(mesh->walkable(op.vec("p", glm::vec3(0.0f)))));
        else if (kind == "line") out.push_back(boolV(mesh->lineWalkable(op.vec("a", glm::vec3(0.0f)), op.vec("b", glm::vec3(0.0f)))));
        else if (kind == "nearest") {
            const auto p = mesh->nearestPoint(op.vec("p", glm::vec3(0.0f)), op.f("max", 2.0f));
            out.push_back(recV({{"found", boolV(p.has_value())}, {"p", vecV(p ? *p : glm::vec3(0.0f))}}));
        } else if (kind == "addObs" || kind == "moveObs") {
            const glm::vec3 c = op.vec("center", glm::vec3(0.0f));
            const glm::vec3 h = glm::abs(op.vec("half", glm::vec3(1.0f)));
            Aabb box;
            box.grow(c - h);
            box.grow(c + h);
            if (kind == "addObs") {
                ids.push_back(mesh->addObstacle(box));
                out.push_back(intV(static_cast<int64_t>(ids.size()) - 1));
            } else {
                const size_t which = ids.empty() ? 0 : static_cast<size_t>(op.integer("id", 0)) % ids.size();
                out.push_back(boolV(!ids.empty() && mesh->moveObstacle(ids[which], box)));
            }
        } else if (kind == "removeObs") {
            const size_t which = ids.empty() ? 0 : static_cast<size_t>(op.integer("id", 0)) % ids.size();
            out.push_back(boolV(!ids.empty() && mesh->removeObstacle(ids[which])));
        } else if (kind == "rebuild") out.push_back(intV(static_cast<int64_t>(mesh->rebuildDirty())));
        else if (kind == "rebuildAll") {
            mesh->rebuildAll();
            out.push_back(Value::unit());
        } else if (kind == "hash") out.push_back(intV(static_cast<int64_t>(mesh->hash() & 0x7fffffffffffffffULL)));
        else if (kind == "dirty") out.push_back(intV(static_cast<int64_t>(mesh->dirtyCount())));
        else if (kind == "walkableCount") out.push_back(intV(static_cast<int64_t>(mesh->walkableCount())));
        else if (kind == "version") out.push_back(intV(static_cast<int64_t>(mesh->version())));
        else throw BentError(loc, "navEval: unknown op '" + kind + "'");
    }
    return listV(std::move(out));
}

const char* stateName(AgentState s) {
    switch (s) {
    case AgentState::Idle: return "idle";
    case AgentState::Moving: return "moving";
    case AgentState::Arrived: return "arrived";
    case AgentState::Failed: return "failed";
    }
    return "?";
}

Value navCrowd(const Value& specValue, const Value& agentsValue, const Value& optsValue, const SrcLoc& loc) {
    const Rec spec = rec(specValue, loc);
    const Rec opts = rec(optsValue, loc);
    std::vector<uint32_t> ids;
    std::unique_ptr<NavMesh> mesh = makeNav(spec, ids);
    std::vector<NavAgent> agents;
    for (const Value& av : agentsValue.items()) {
        const Rec a = rec(av, loc);
        NavAgent agent;
        agent.position = a.vec("pos", glm::vec3(0.0f));
        agent.radius = a.f("radius", 0.4f);
        agent.maxSpeed = a.f("maxSpeed", 3.0f);
        agent.velocity = a.vec("vel", glm::vec3(0.0f));
        agents.push_back(agent);
        if (a.has("target")) navSetTarget(*mesh, agents.back(), a.vec("target", glm::vec3(0.0f)));
    }
    NavCrowd crowd;
    const bool useCrowd = opts.flag("crowd", false);
    const float dt = opts.f("dt", 1.0f / 60.0f);
    const int64_t steps = opts.integer("steps", 60);
    const int64_t every = std::max<int64_t>(1, opts.integer("record", 1));
    const auto snapshot = [&]() {
        ValueList list;
        for (const NavAgent& a : agents) list.push_back(recV({{"pos", vecV(a.position)}, {"vel", vecV(a.velocity)}, {"state", Value::string(stateName(a.state))}, {"repaths", intV(a.repaths)}}));
        return listV(std::move(list));
    };
    ValueList frames;
    frames.push_back(snapshot());
    for (int64_t i = 0; i < steps; ++i) {
        if (useCrowd) crowd.step(*mesh, agents, dt);
        else navStepAgents(*mesh, agents, dt);
        if ((i + 1) % every == 0) frames.push_back(snapshot());
    }
    return recV({{"frames", listV(std::move(frames))}});
}

Status runSlow(AiContext& ctx) {
    const double n = ctx.blackboard.number("slow_n", 0.0) + 1.0;
    if (n < 3.0) {
        ctx.blackboard.setNumber("slow_n", n);
        return Status::Running;
    }
    ctx.blackboard.setNumber("slow_n", 0.0);
    return Status::Success;
}

Value aiRun(const Value& graphText, const Value& specValue, const SrcLoc& loc) {
    const Rec spec = rec(specValue, loc);
    BehaviorGraph graph;
    std::string error;
    if (!graph.loadText(argString(graphText, loc, "aiRun graph"), &error)) return recV({{"ok", boolV(false)}, {"error", Value::string(error)}});
    ActionRegistry actions;
    actions.add("ok", [](AiContext&) { return Status::Success; });
    actions.add("bad", [](AiContext&) { return Status::Failure; });
    actions.add("slow", runSlow);
    actions.add("inc", [](AiContext& c) {
        c.blackboard.setNumber("count", c.blackboard.number("count", 0.0) + 1.0);
        return Status::Success;
    });
    actions.add("flip", [](AiContext& c) {
        c.blackboard.setBool("flag", !c.blackboard.boolean("flag", false));
        return Status::Success;
    });
    actions.add("rand", [](AiContext& c) {
        const float r = c.rng.uniform();
        c.blackboard.setNumber("r", r);
        return r < 0.5f ? Status::Success : Status::Failure;
    });
    Pcg32 rng(static_cast<uint64_t>(spec.integer("seed", 1)));
    BehaviorRunner runner(graph, actions, rng, static_cast<uint32_t>(spec.integer("agent", 0)));
    runner.trace().setCapacity(100000);
    for (const auto& [key, value] : spec.has("bb") ? spec.get("bb")->rec->fields : std::vector<std::pair<std::string, Value>>{}) {
        if (value.kind == Kind::Bool) runner.blackboard().setBool(key, value.b);
        else if (value.isNumber()) runner.blackboard().setNumber(key, value.num());
        else if (value.kind == Kind::Str) runner.blackboard().setString(key, value.text());
    }
    const int64_t ticks = spec.integer("ticks", 10);
    const float dt = spec.f("dt", 0.1f);
    ValueList statuses;
    ValueList states;
    for (int64_t i = 0; i < ticks; ++i) {
        const Status s = runner.tick(dt);
        statuses.push_back(Value::string(s == Status::Success ? "success" : s == Status::Failure ? "failure" : "running"));
        states.push_back(Value::string(runner.stateOf(spec.str("fsm", ""))));
    }
    ValueList events;
    for (const TraceEvent& e : runner.trace().events()) {
        ValueList options;
        for (const TraceOption& o : e.options) {
            ValueList cons;
            for (const TraceConsideration& c : o.considerations) cons.push_back(recV({{"input", Value::string(c.input)}, {"value", numV(c.value)}, {"score", numV(c.score)}}));
            options.push_back(recV({{"name", Value::string(o.name)}, {"score", numV(o.score)}, {"available", boolV(o.available)}, {"considerations", listV(std::move(cons))}}));
        }
        events.push_back(recV({{"tick", intV(static_cast<int64_t>(e.tick))}, {"node", Value::string(e.node)}, {"kind", Value::string(e.kind)}, {"winner", Value::string(e.winner)}, {"reason", Value::string(e.reason)}, {"options", listV(std::move(options))}}));
    }
    ValueList path;
    for (const std::string& p : runner.activePath()) path.push_back(Value::string(p));
    return recV({{"ok", boolV(true)},
                 {"statuses", listV(std::move(statuses))},
                 {"states", listV(std::move(states))},
                 {"events", listV(std::move(events))},
                 {"count", numV(runner.blackboard().number("count", 0.0))},
                 {"r", numV(runner.blackboard().number("r", -1.0))},
                 {"flag", boolV(runner.blackboard().boolean("flag", false))},
                 {"blackboard", Value::string(runner.blackboard().toJson().dump())},
                 {"trace", Value::string(runner.trace().toJson().dump())},
                 {"stateHash", intV(static_cast<int64_t>(runner.stateHash() & 0x7fffffffffffffffULL))},
                 {"tickCount", intV(static_cast<int64_t>(runner.tickCount()))},
                 {"activePath", listV(std::move(path))}});
}


Skeleton skelOf(const Value& v, const SrcLoc& loc) {
    Skeleton s;
    int n = 0;
    for (const Value& jv : v.items()) {
        const Rec j = rec(jv, loc);
        s.add(j.str("name", "j" + std::to_string(n++)), static_cast<int>(j.integer("parent", -1)), j.vec("pos", glm::vec3(0.0f)), j.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)), j.vec("scale", glm::vec3(1.0f)));
    }
    return s;
}

std::vector<Vec3Key> vecKeys(const ValueList& list, const SrcLoc& loc) {
    std::vector<Vec3Key> out;
    for (const Value& kv : list) out.push_back({static_cast<float>(argNumber(kv.items()[0], loc, "key time")), vecOf(kv.items()[1], loc, "key")});
    return out;
}

SkeletalClip clipOf(const Value& v, const SrcLoc& loc) {
    const Rec c = rec(v, loc);
    SkeletalClip clip;
    clip.name = "clip";
    clip.duration = c.f("duration", 1.0f);
    clip.loop = c.flag("loop", true);
    for (const Value& tv : c.list("tracks")) {
        const Rec t = rec(tv, loc);
        JointTrack track;
        track.joint = static_cast<int>(t.integer("joint", 0));
        track.position = vecKeys(t.list("pos"), loc);
        track.scale = vecKeys(t.list("scale"), loc);
        for (const Value& kv : t.list("rot")) track.rotation.push_back({static_cast<float>(argNumber(kv.items()[0], loc, "key time")), quatOf(kv.items()[1], loc, "key")});
        clip.tracks.push_back(std::move(track));
    }
    return clip;
}

Value poseV(const Pose& pose) {
    ValueList out;
    for (const JointPose& j : pose.joints) out.push_back(recV({{"pos", vecV(j.position)}, {"rot", quatV(j.rotation)}, {"scale", vecV(j.scale)}}));
    return listV(std::move(out));
}

Pose poseOf(const Value& v, const SrcLoc& loc) {
    Pose pose;
    for (const Value& jv : v.items()) {
        const Rec j = rec(jv, loc);
        JointPose jp;
        jp.position = j.vec("pos", glm::vec3(0.0f));
        jp.rotation = j.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        jp.scale = j.vec("scale", glm::vec3(1.0f));
        pose.joints.push_back(jp);
    }
    return pose;
}

Value modelV(const ModelPose& m) {
    ValueList out;
    for (size_t i = 0; i < m.position.size(); ++i) out.push_back(recV({{"pos", vecV(m.position[i])}, {"rot", quatV(m.rotation[i])}}));
    return listV(std::move(out));
}

Value skelAnimate(const Value& skelValue, const Value& clipsValue, const Value& script, const SrcLoc& loc) {
    const Skeleton skeleton = skelOf(skelValue, loc);
    SkeletalLibrary library;
    for (const Value& cv : clipsValue.items()) library.add(clipOf(cv, loc));
    SkeletalAnimator animator(skeleton, library);
    ValueList poses;
    for (const Value& opv : script.items()) {
        const Rec op = rec(opv, loc);
        const std::string kind = op.str("op", "update");
        if (kind == "play") animator.play(static_cast<uint32_t>(op.integer("clip", 0)), op.f("fade", 0.2f));
        else if (kind == "space") {
            std::vector<BlendSample> samples;
            for (const Value& sv : op.list("samples")) samples.push_back({static_cast<float>(argNumber(sv.items()[0], loc, "space x")), static_cast<uint32_t>(argInt(sv.items()[1], loc, "space clip"))});
            animator.playBlendSpace(std::move(samples), op.f("param", 0.0f), op.f("fade", 0.2f));
        } else if (kind == "param") animator.setBlendParameter(op.f("v", 0.0f));
        else if (kind == "speed") animator.setSpeed(op.f("v", 1.0f));
        else if (kind == "update") {
            const int64_t n = op.integer("n", 1);
            for (int64_t i = 0; i < n; ++i) animator.update(op.f("dt", 1.0f / 60.0f));
            poses.push_back(recV({{"pose", poseV(animator.pose())}, {"fade", numV(animator.fadeRemaining())}, {"time", numV(animator.time())}}));
        } else throw BentError(loc, "skelAnimate: unknown op '" + kind + "'");
    }
    return listV(std::move(poses));
}

float matMaxDiff(const glm::mat4& a, const glm::mat4& b) {
    float d = 0.0f;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r) d = std::max(d, std::abs(a[c][r] - b[c][r]));
    return d;
}

glm::vec3 posAt(const std::vector<float>& v, size_t i) { return glm::vec3(v[i * kVertexFloats], v[i * kVertexFloats + 1], v[i * kVertexFloats + 2]); }

Value skinCheck(const Value& optsValue, const Value& specValue, const SrcLoc& loc) {
    const Rec o = rec(optsValue, loc);
    CharacterOptions options;
    options.height = o.f("height", 1.8f);
    options.radialSegments = static_cast<int>(o.integer("radial", 8));
    options.ringsPerBone = static_cast<int>(o.integer("rings", 2));
    options.headMorphs = o.flag("morphs", true);
    const DynamicMesh mesh = makeProceduralCharacter(options);
    const Rec s = rec(specValue, loc);
    const Skeleton skel = skeletonFromSkin(mesh);
    Pose pose = Pose::bind(skel);
    const std::string mode = s.str("mode", "bind");
    Pcg32 rng(static_cast<uint64_t>(s.integer("seed", 1)));
    if (mode == "random") {
        const float maxAngle = s.f("angle", 1.0f);
        for (JointPose& j : pose.joints) {
            const glm::vec3 axis = glm::normalize(glm::vec3(rng.range(-1, 1), rng.range(-1, 1), rng.range(-1, 1)) + glm::vec3(1e-3f));
            j.rotation = glm::normalize(glm::angleAxis(rng.range(-maxAngle, maxAngle), axis) * j.rotation);
        }
    } else if (mode == "walk" || mode == "wave") {
        const SkeletalClip clip = mode == "walk" ? makeWalkClip(skel) : makeWaveClip(skel);
        sampleSkeletalClip(skel, clip, s.f("time", 0.0f), pose);
    }
    std::vector<glm::mat4> palette;
    computeSkinPalette(skel, pose, mesh, palette);
    std::vector<float> morph(mesh.morphs.size(), 0.0f);
    if (s.flag("morph", false)) {
        for (float& w : morph) w = rng.uniform();
    }
    DeformSettings none;
    std::vector<float> out;
    deformMeshCpu(mesh, palette, morph, none, out);
    float refErr = 0.0f;
    float rigidErr = 0.0f;
    float nanCount = 0.0f;
    std::map<uint32_t, std::pair<size_t, glm::vec3>> anchor;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) {
        glm::vec3 p = posAt(mesh.rest.vertices, i);
        for (size_t k = 0; k < mesh.morphs.size(); ++k) {
            if (i < mesh.morphs[k].positionDelta.size()) p += morph[k] * mesh.morphs[k].positionDelta[i];
        }
        glm::vec3 skinned(0.0f);
        float wsum = 0.0f;
        if (mesh.skinned()) {
            for (int j = 0; j < 4; ++j) {
                const float w = mesh.skin[i].weights[static_cast<size_t>(j)];
                if (w <= 0.0f) continue;
                skinned += w * glm::vec3(palette[mesh.skin[i].joints[static_cast<size_t>(j)]] * glm::vec4(p, 1.0f));
                wsum += w;
            }
        } else skinned = p;
        const glm::vec3 got = posAt(out, i);
        if (!std::isfinite(got.x) || !std::isfinite(got.y) || !std::isfinite(got.z)) nanCount += 1.0f;
        else refErr = std::max(refErr, glm::length(got - skinned));
        if (mesh.skinned() && mesh.skin[i].weights[0] > 0.999f && morph.empty() == false) {
        }
        if (mesh.skinned() && mesh.skin[i].weights[0] > 0.9999f && !s.flag("morph", false)) {
            const uint32_t joint = mesh.skin[i].joints[0];
            const auto it = anchor.find(joint);
            if (it == anchor.end()) anchor[joint] = {i, got};
            else {
                const float restD = glm::length(posAt(mesh.rest.vertices, i) - posAt(mesh.rest.vertices, it->second.first));
                rigidErr = std::max(rigidErr, std::abs(glm::length(got - it->second.second) - restD));
            }
        }
    }
    float identErr = 0.0f;
    float orthoErr = 0.0f;
    for (const glm::mat4& m : palette) {
        identErr = std::max(identErr, matMaxDiff(m, glm::mat4(1.0f)));
        const glm::mat3 r(m);
        const glm::mat3 rtr = glm::transpose(r) * r;
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr) orthoErr = std::max(orthoErr, std::abs(rtr[c][rr] - (c == rr ? 1.0f : 0.0f)));
        orthoErr = std::max(orthoErr, std::abs(m[3][3] - 1.0f));
    }
    const SkinBounds bounds = buildSkinBounds(mesh);
    const glm::vec4 sphere = computeDynamicBounds(bounds, palette, morph, none);
    float excess = -1.0e30f;
    for (size_t i = 0; i < mesh.vertexCount(); ++i) excess = std::max(excess, glm::length(posAt(out, i) - glm::vec3(sphere)) - sphere.w);
    float morphErr = 0.0f;
    float morphLinErr = 0.0f;
    {
        std::vector<glm::mat4> bindPalette;
        computeSkinPalette(skel, Pose::bind(skel), mesh, bindPalette);
        std::vector<float> zero(mesh.morphs.size(), 0.0f);
        std::vector<float> w(mesh.morphs.size());
        for (float& x : w) x = rng.uniform();
        std::vector<float> half(w);
        const float alpha = 0.25f + 0.5f * rng.uniform();
        for (float& x : half) x *= alpha;
        std::vector<float> rest;
        std::vector<float> full;
        std::vector<float> part;
        deformMeshCpu(mesh, bindPalette, zero, none, rest);
        deformMeshCpu(mesh, bindPalette, w, none, full);
        deformMeshCpu(mesh, bindPalette, half, none, part);
        for (size_t i = 0; i < mesh.vertexCount(); ++i) {
            glm::vec3 expected = posAt(rest, i);
            for (size_t k = 0; k < mesh.morphs.size(); ++k) {
                if (i < mesh.morphs[k].positionDelta.size()) expected += w[k] * mesh.morphs[k].positionDelta[i];
            }
            morphErr = std::max(morphErr, glm::length(posAt(full, i) - expected));
            morphLinErr = std::max(morphLinErr, glm::length((posAt(part, i) - posAt(rest, i)) - alpha * (posAt(full, i) - posAt(rest, i))));
        }
    }
    float weightErr = 0.0f;
    uint32_t maxJoint = 0;
    for (const SkinVertex& v : mesh.skin) {
        float sum = 0.0f;
        for (int j = 0; j < 4; ++j) {
            sum += v.weights[static_cast<size_t>(j)];
            if (v.weights[static_cast<size_t>(j)] > 0.0f) maxJoint = std::max(maxJoint, v.joints[static_cast<size_t>(j)]);
        }
        weightErr = std::max(weightErr, std::abs(sum - 1.0f));
    }
    std::string error;
    ValueList names;
    for (const MorphTarget& m : mesh.morphs) names.push_back(Value::string(m.name));
    return recV({{"joints", intV(static_cast<int64_t>(mesh.joints.size()))},
                 {"vertices", intV(static_cast<int64_t>(mesh.vertexCount()))},
                 {"morphs", intV(static_cast<int64_t>(mesh.morphs.size()))},
                 {"valid", boolV(validateDynamicMesh(mesh, &error))},
                 {"weightErr", numV(weightErr)},
                 {"maxJoint", intV(maxJoint)},
                 {"identErr", numV(identErr)},
                 {"orthoErr", numV(orthoErr)},
                 {"refErr", numV(refErr)},
                 {"rigidErr", numV(rigidErr)},
                 {"boundsExcess", numV(excess)},
                 {"morphErr", numV(morphErr)},
                 {"morphLinErr", numV(morphLinErr)},
                 {"nonFinite", numV(nanCount)},
                 {"morphNames", listV(std::move(names))}});
}

Value skinNormalize(const Value& listValue, const Value& jointCount, const SrcLoc& loc) {
    std::vector<SkinVertex> skin;
    for (const Value& v : listValue.items()) {
        const ValueList& t = v.items();
        SkinVertex sv;
        for (size_t i = 0; i < 4; ++i) {
            sv.joints[i] = static_cast<uint32_t>(std::max<int64_t>(0, argInt(t[i], loc, "joint")));
            sv.weights[i] = static_cast<float>(argNumber(t[4 + i], loc, "weight"));
        }
        skin.push_back(sv);
    }
    const size_t repaired = normalizeSkinWeights(skin, static_cast<uint32_t>(argInt(jointCount, loc, "jointCount")));
    ValueList out;
    for (const SkinVertex& sv : skin) {
        ValueList row;
        for (size_t i = 0; i < 4; ++i) row.push_back(intV(sv.joints[i]));
        for (size_t i = 0; i < 4; ++i) row.push_back(numV(sv.weights[i]));
        out.push_back(listV(std::move(row)));
    }
    return recV({{"skin", listV(std::move(out))}, {"repaired", intV(static_cast<int64_t>(repaired))}});
}

Value faceNames() {
    ValueList out;
    for (const char* n : kArkitBlendshapes) out.push_back(Value::string(n));
    return listV(std::move(out));
}

BlendshapeWeights weightsOf(const Value& v, const SrcLoc& loc) {
    BlendshapeWeights w = blendshapeZero();
    const ValueList& items = v.items();
    for (size_t i = 0; i < w.size() && i < items.size(); ++i) w[i] = static_cast<float>(argNumber(items[i], loc, "weight"));
    return w;
}

Value weightsV(const BlendshapeWeights& w) {
    ValueList out;
    for (float f : w) out.push_back(numV(f));
    return listV(std::move(out));
}

Value faceMorph(const Value& weights, const SrcLoc& loc) {
    const DynamicMesh mesh = makeProceduralCharacter();
    std::vector<float> out;
    morphWeightsFromBlendshapes(mesh, weightsOf(weights, loc), out);
    ValueList list;
    ValueList names;
    ValueList channels;
    for (size_t i = 0; i < out.size(); ++i) {
        list.push_back(numV(out[i]));
        names.push_back(Value::string(mesh.morphs[i].name));
        channels.push_back(intV(blendshapeIndex(mesh.morphs[i].name)));
    }
    return recV({{"weights", listV(std::move(list))}, {"names", listV(std::move(names))}, {"channels", listV(std::move(channels))}});
}

Value faceClipSample(const Value& clipValue, const Value& time, const SrcLoc& loc) {
    const Rec c = rec(clipValue, loc);
    BlendshapeClip clip;
    clip.duration = c.f("duration", 1.0f);
    clip.loop = c.flag("loop", false);
    for (const Value& cv : c.list("curves")) {
        const Rec cr = rec(cv, loc);
        BlendshapeCurve curve;
        curve.channel = static_cast<int>(cr.integer("channel", 0));
        for (const Value& kv : cr.list("keys")) curve.keys.push_back({static_cast<float>(argNumber(kv.items()[0], loc, "t")), static_cast<float>(argNumber(kv.items()[1], loc, "v"))});
        clip.curves.push_back(std::move(curve));
    }
    BlendshapeWeights out = blendshapeZero();
    clip.sample(static_cast<float>(argNumber(time, loc, "time")), out);
    return weightsV(out);
}

Value faceCombine(const Value& a, const Value& b, const Value& mode, const Value& weight, const SrcLoc& loc) {
    BlendshapeWeights base = weightsOf(a, loc);
    const BlendshapeWeights layer = weightsOf(b, loc);
    if (argString(mode, loc, "mode") == "max") blendshapeMax(base, layer);
    else blendshapeAdd(base, layer, static_cast<float>(argNumber(weight, loc, "weight")));
    return weightsV(base);
}

Value ikSolve(const Value& specValue, const SrcLoc& loc) {
    const Rec s = rec(specValue, loc);
    IkChainState chain;
    std::vector<glm::vec3> points;
    for (const Value& p : s.list("points")) points.push_back(vecOf(p, loc, "points"));
    chain.setPoints(points.data(), static_cast<int>(points.size()));
    ikMeasureLengths(chain);
    if (s.has("lengths")) {
        size_t i = 0;
        for (const Value& l : s.list("lengths")) chain.lengths[i++] = static_cast<float>(argNumber(l, loc, "lengths"));
    }
    size_t li = 0;
    for (const Value& lv : s.list("limits")) {
        const Rec l = rec(lv, loc);
        IkJointLimit limit;
        limit.hinge = l.flag("hinge", false);
        limit.minAngle = l.f("min", -kIkPi);
        limit.maxAngle = l.f("max", kIkPi);
        limit.cone = l.f("cone", kIkPi);
        if (li < chain.limits.size()) chain.limits[li++] = limit;
    }
    chain.bendAxis = s.vec("bend", glm::vec3(0.0f, 0.0f, 1.0f));
    IkSettings settings;
    settings.iterations = static_cast<int>(s.integer("iterations", 12));
    settings.tolerance = s.f("tolerance", 1.0e-3f);
    if (s.has("pole")) {
        settings.hasPole = true;
        settings.pole = s.vec("pole", glm::vec3(0.0f));
    }
    if (s.has("base")) settings.baseDirection = s.vec("base", glm::vec3(0.0f));
    const glm::vec3 target = s.vec("target", glm::vec3(0.0f));
    const std::string algo = s.str("algo", "auto");
    IkResult result;
    if (algo == "fabrik") result = solveFabrik(chain, target, settings);
    else if (algo == "ccd") result = solveCcd(chain, target, settings);
    else if (algo == "two") result = solveTwoBoneAnalytic(chain, target, settings);
    else result = solveIk(chain, target, settings);
    ValueList out;
    ValueList lengths;
    ValueList angles;
    ValueList signedAngles;
    for (int i = 0; i < chain.count; ++i) out.push_back(vecV(chain.points[static_cast<size_t>(i)]));
    for (int i = 0; i + 1 < chain.count; ++i) lengths.push_back(numV(glm::length(chain.points[static_cast<size_t>(i + 1)] - chain.points[static_cast<size_t>(i)])));
    for (int i = 1; i + 1 < chain.count; ++i) {
        const glm::vec3 d0 = chain.points[static_cast<size_t>(i)] - chain.points[static_cast<size_t>(i - 1)];
        const glm::vec3 d1 = chain.points[static_cast<size_t>(i + 1)] - chain.points[static_cast<size_t>(i)];
        const float l0 = glm::length(d0);
        const float l1 = glm::length(d1);
        if (l0 < 1e-9f || l1 < 1e-9f) {
            angles.push_back(numV(0));
            signedAngles.push_back(numV(0));
            continue;
        }
        angles.push_back(numV(std::acos(std::clamp(glm::dot(d0 / l0, d1 / l1), -1.0f, 1.0f))));
        signedAngles.push_back(numV(ikSignedAngle(d0 / l0, d1 / l1, chain.bendAxis)));
    }
    return recV({{"points", listV(std::move(out))}, {"lengths", listV(std::move(lengths))}, {"angles", listV(std::move(angles))}, {"signed", listV(std::move(signedAngles))}, {"reach", numV(ikChainReach(chain))}, {"error", numV(result.error)}, {"reached", boolV(result.reached)}, {"iterations", intV(result.iterations)}});
}

Value skelTwoBone(const Value& skelValue, const Value& chainValue, const Value& target, const Value& pole, const Value& weight, const SrcLoc& loc) {
    const Skeleton skel = skelOf(skelValue, loc);
    Pose pose = Pose::bind(skel);
    const Rec c = rec(chainValue, loc);
    TwoBoneChain chain{static_cast<int>(c.integer("root", 0)), static_cast<int>(c.integer("mid", 1)), static_cast<int>(c.integer("tip", 2))};
    const ModelPose before = computeModelPose(skel, pose);
    std::optional<glm::vec3> poleOpt;
    if (pole.kind == Kind::Vec3) poleOpt = vecOf(pole, loc, "pole");
    const bool ok = solveTwoBone(skel, pose, chain, vecOf(target, loc, "target"), poleOpt, static_cast<float>(argNumber(weight, loc, "weight")));
    const ModelPose after = computeModelPose(skel, pose);
    return recV({{"ok", boolV(ok)}, {"before", modelV(before)}, {"after", modelV(after)}, {"pose", poseV(pose)}});
}

std::shared_ptr<AudioClip> clipFor(const Rec& v) { return AudioClip::sine("tone", v.f("freq", 440.0f), v.f("seconds", 1.0f), v.f("amp", 0.5f), 48000); }

VoiceParams voiceOf(const Rec& v) {
    VoiceParams p;
    p.clip = clipFor(v);
    p.bus = v.str("bus", "master");
    p.loop = v.flag("loop", true);
    p.gain = v.f("gain", 1.0f);
    p.pitch = v.f("pitch", 1.0f);
    p.spatial = v.flag("spatial", true);
    p.position = v.vec("pos", glm::vec3(0.0f));
    p.minDistance = v.f("min", 1.0f);
    p.maxDistance = v.f("max", 50.0f);
    p.reverbSend = v.f("reverbSend", 0.0f);
    p.startTime = v.f("start", 0.0f);
    return p;
}

Value audioRender(const Value& specValue, const SrcLoc& loc) {
    const Rec s = rec(specValue, loc);
    AudioMixer mixer(48000);
    for (const Value& bv : s.list("buses")) {
        const Rec b = rec(bv, loc);
        const std::string name = b.str("name", "bus");
        mixer.addBus(name, b.str("parent", "master"));
        mixer.setBusGain(name, b.f("gain", 1.0f));
        mixer.setBusMute(name, b.flag("mute", false));
    }
    if (s.has("masterGain")) mixer.setBusGain("master", s.f("masterGain", 1.0f));
    for (const Value& dv : s.list("ducks")) {
        const Rec d = rec(dv, loc);
        mixer.addDuck(d.str("trigger", "master"), d.str("target", "master"), d.f("db", -12.0f), d.f("threshold", 0.02f), d.f("attack", 0.05f), d.f("release", 0.4f));
    }
    mixer.setListener(s.vec("listener", glm::vec3(0.0f)), s.vec("forward", glm::vec3(0.0f, 0.0f, 1.0f)));
    for (const Value& vv : s.list("voices")) mixer.play(voiceOf(rec(vv, loc)));
    const int64_t blocks = s.integer("blocks", 4);
    const uint32_t frames = static_cast<uint32_t>(s.integer("frames", 512));
    std::vector<float> buffer(static_cast<size_t>(frames) * 2);
    double sumL = 0.0;
    double sumR = 0.0;
    double peak = 0.0;
    double peakL = 0.0;
    double peakR = 0.0;
    bool bad = false;
    ValueList blockRms;
    ValueList duckTrace;
    for (int64_t b = 0; b < blocks; ++b) {
        mixer.update();
        mixer.render(buffer.data(), frames);
        double bl = 0.0;
        for (uint32_t i = 0; i < frames; ++i) {
            const double l = buffer[2 * i];
            const double r = buffer[2 * i + 1];
            if (!std::isfinite(l) || !std::isfinite(r)) bad = true;
            sumL += l * l;
            sumR += r * r;
            bl += l * l + r * r;
            peakL = std::max(peakL, std::abs(l));
            peakR = std::max(peakR, std::abs(r));
        }
        blockRms.push_back(numV(std::sqrt(bl / (2.0 * frames))));
        ValueList duck;
        for (const Value& dv : s.list("ducks")) duck.push_back(numV(mixer.busDuck(rec(dv, loc).str("target", "master"))));
        duckTrace.push_back(listV(std::move(duck)));
    }
    peak = std::max(peakL, peakR);
    const double n = static_cast<double>(blocks) * frames;
    ValueList levels;
    for (const Value& bv : s.list("buses")) {
        const std::string name = rec(bv, loc).str("name", "bus");
        levels.push_back(recV({{"name", Value::string(name)}, {"level", numV(mixer.busLevel(name))}, {"duck", numV(mixer.busDuck(name))}, {"gain", numV(mixer.busGain(name))}}));
    }
    return recV({{"peak", numV(peak)}, {"peakL", numV(peakL)}, {"peakR", numV(peakR)}, {"rmsL", numV(std::sqrt(sumL / n))}, {"rmsR", numV(std::sqrt(sumR / n))}, {"nonFinite", boolV(bad)}, {"voices", intV(mixer.voiceCount())}, {"blockRms", listV(std::move(blockRms))}, {"ducks", listV(std::move(duckTrace))}, {"buses", listV(std::move(levels))}, {"masterLevel", numV(mixer.busLevel("master"))}});
}

Value audioTrace(const Value& specValue, const SrcLoc& loc) {
    const Rec s = rec(specValue, loc);
    AudioPropagation prop;
    TriangleSoup soup;
    for (const Value& bv : s.list("boxes")) {
        const Rec b = rec(bv, loc);
        soup.addBox(glm::translate(glm::mat4(1.0f), b.vec("center", glm::vec3(0.0f))), glm::abs(b.vec("half", glm::vec3(1.0f))));
    }
    prop.setScene(soup);
    AudioPropagationConfig& cfg = prop.config();
    const Value configValue = s.has("config") ? *s.get("config") : Value::unit();
    const Rec c = rec(configValue, loc);
    cfg.maxHops = static_cast<int>(c.integer("maxHops", cfg.maxHops));
    cfg.maxDistance = c.f("maxDistance", cfg.maxDistance);
    cfg.occlusionRays = static_cast<int>(c.integer("occlusionRays", cfg.occlusionRays));
    cfg.raysPerFrame = static_cast<uint32_t>(c.integer("raysPerFrame", cfg.raysPerFrame));
    cfg.referenceDistance = c.f("referenceDistance", cfg.referenceDistance);
    std::vector<AudioHop> hops;
    for (const Value& hv : s.list("hops")) {
        const Rec h = rec(hv, loc);
        AudioHop hop;
        hop.entry = h.vec("entry", glm::vec3(0.0f));
        hop.exit = h.vec("exit", glm::vec3(0.0f));
        hop.cost = h.f("cost", 0.0f);
        hop.id = static_cast<int>(h.integer("id", static_cast<int64_t>(hops.size())));
        hops.push_back(hop);
    }
    if (!hops.empty()) prop.setHopHook([hops](const glm::vec3&, std::vector<AudioHop>& out) { out.insert(out.end(), hops.begin(), hops.end()); });
    const glm::vec3 listener = s.vec("listener", glm::vec3(0.0f));
    prop.beginFrame(listener);
    ValueList results;
    for (const Value& sv : s.list("sources")) {
        const AudioPath path = prop.trace(listener, vecOf(sv, loc, "source"));
        results.push_back(recV({{"audible", boolV(path.audible)}, {"direct", boolV(path.direct)}, {"viaHop", boolV(path.viaHop)}, {"hops", intV(path.hops)}, {"hopId", intV(path.hopId)}, {"directOcclusion", numV(path.directOcclusion)}, {"occlusion", numV(path.occlusion)}, {"pathLength", numV(path.pathLength)}, {"attenuation", numV(path.attenuation)}, {"lowpassHz", numV(path.lowpassHz)}, {"arrival", vecV(path.arrivalDirection)}, {"reflections", intV(static_cast<int64_t>(path.reflections.size()))}, {"reverbSend", numV(path.reverbSend)}}));
    }
    return listV(std::move(results));
}

const char* lineEventName(LineEvent e) {
    switch (e) {
    case LineEvent::Started: return "started";
    case LineEvent::Interrupted: return "interrupted";
    case LineEvent::Resumed: return "resumed";
    case LineEvent::Finished: return "finished";
    case LineEvent::Dropped: return "dropped";
    }
    return "?";
}

Value audioSched(const Value& specValue, const SrcLoc& loc) {
    const Rec s = rec(specValue, loc);
    AudioMixer mixer(48000);
    VoiceScheduler scheduler(mixer);
    mixer.setListener(s.vec("listener", glm::vec3(0.0f)), glm::vec3(0.0f, 0.0f, 1.0f));
    scheduler.setSpeakerPosition(s.vec("speaker", glm::vec3(0.0f)));
    const int64_t ticks = s.integer("ticks", 120);
    const float dt = 1.0f / 60.0f;
    std::vector<float> buffer(800 * 2);
    ValueList log;
    for (int64_t t = 0; t < ticks; ++t) {
        for (const Value& lv : s.list("lines")) {
            const Rec l = rec(lv, loc);
            if (l.integer("at", 0) != t) continue;
            VoiceLine line;
            line.id = l.str("id", "line");
            line.clip = AudioClip::sine(line.id, 440.0f, l.f("seconds", 1.0f), 0.5f, 48000);
            line.priority = static_cast<int>(l.integer("priority", 0));
            line.interruptible = l.flag("interruptible", true);
            line.resumable = l.flag("resumable", true);
            line.positional = l.flag("positional", false);
            line.position = l.vec("pos", glm::vec3(0.0f));
            line.maxDistance = l.f("maxDistance", 15.0f);
            line.resumeDistance = l.f("resumeDistance", 12.0f);
            scheduler.enqueue(std::move(line));
        }
        if (s.has("listenerAt") && s.integer("listenerTick", -1) == t) mixer.setListener(s.vec("listenerAt", glm::vec3(0.0f)), glm::vec3(0.0f, 0.0f, 1.0f));
        scheduler.update(dt);
        mixer.update();
        mixer.render(buffer.data(), 800);
        const VoiceLine* cur = scheduler.current();
        log.push_back(recV({{"current", Value::string(cur ? cur->id : std::string())}, {"pending", intV(static_cast<int64_t>(scheduler.pending()))}, {"voices", intV(mixer.voiceCount())}}));
    }
    ValueList events;
    for (const LineEventRecord& e : scheduler.events()) events.push_back(recV({{"type", Value::string(lineEventName(e.type))}, {"id", Value::string(e.id)}, {"clipTime", numV(e.clipTime)}, {"update", intV(static_cast<int64_t>(e.update))}}));
    return recV({{"log", listV(std::move(log))}, {"events", listV(std::move(events))}});
}

}

void registerExtSimBuiltins(Program& program) {
    const auto reg = [&](const char* name, size_t arity, std::function<Value(const ValueList&, const SrcLoc&)> fn) {
        program.native(name, [name, arity, fn](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
            if (args.size() != arity) throw BentError(loc, std::string(name) + "() takes " + std::to_string(arity) + " argument(s)");
            return fn(args, loc);
        });
    };
    reg("navEval", 2, [](const ValueList& a, const SrcLoc& loc) { return navEval(a[0], a[1], loc); });
    reg("navCrowd", 3, [](const ValueList& a, const SrcLoc& loc) { return navCrowd(a[0], a[1], a[2], loc); });
    reg("aiRun", 2, [](const ValueList& a, const SrcLoc& loc) { return aiRun(a[0], a[1], loc); });
    reg("skelSample", 3, [](const ValueList& a, const SrcLoc& loc) {
        const Skeleton skel = skelOf(a[0], loc);
        Pose pose = Pose::bind(skel);
        sampleSkeletalClip(skel, clipOf(a[1], loc), static_cast<float>(argNumber(a[2], loc, "time")), pose);
        return poseV(pose);
    });
    reg("skelBlend", 5, [](const ValueList& a, const SrcLoc& loc) {
        const Skeleton skel = skelOf(a[0], loc);
        const Pose from = poseOf(a[1], loc);
        const Pose to = poseOf(a[2], loc);
        Pose out = from;
        std::vector<float> mask;
        for (const Value& m : a[4].items()) mask.push_back(static_cast<float>(argNumber(m, loc, "mask")));
        if (mask.empty()) blendPoses(from, to, static_cast<float>(argNumber(a[3], loc, "weight")), out);
        else blendPosesMasked(from, to, mask, static_cast<float>(argNumber(a[3], loc, "weight")), out);
        (void)skel;
        return poseV(out);
    });
    reg("skelModel", 2, [](const ValueList& a, const SrcLoc& loc) { return modelV(computeModelPose(skelOf(a[0], loc), poseOf(a[1], loc))); });
    reg("skelSubtreeMask", 3, [](const ValueList& a, const SrcLoc& loc) {
        ValueList out;
        for (float f : subtreeMask(skelOf(a[0], loc), static_cast<int>(argInt(a[1], loc, "root")), a[2].kind == Kind::Bool && a[2].b)) out.push_back(numV(f));
        return listV(std::move(out));
    });
    reg("skelAnimate", 3, [](const ValueList& a, const SrcLoc& loc) { return skelAnimate(a[0], a[1], a[2], loc); });
    reg("skelTwoBone", 5, [](const ValueList& a, const SrcLoc& loc) { return skelTwoBone(a[0], a[1], a[2], a[3], a[4], loc); });
    reg("skinCheck", 2, [](const ValueList& a, const SrcLoc& loc) { return skinCheck(a[0], a[1], loc); });
    reg("skinNormalize", 2, [](const ValueList& a, const SrcLoc& loc) { return skinNormalize(a[0], a[1], loc); });
    reg("faceNames", 0, [](const ValueList&, const SrcLoc&) { return faceNames(); });
    reg("faceIndex", 1, [](const ValueList& a, const SrcLoc& loc) { return intV(blendshapeIndex(argString(a[0], loc, "name"))); });
    reg("faceMorph", 1, [](const ValueList& a, const SrcLoc& loc) { return faceMorph(a[0], loc); });
    reg("faceClipSample", 2, [](const ValueList& a, const SrcLoc& loc) { return faceClipSample(a[0], a[1], loc); });
    reg("faceCombine", 4, [](const ValueList& a, const SrcLoc& loc) { return faceCombine(a[0], a[1], a[2], a[3], loc); });
    reg("ikSolve", 1, [](const ValueList& a, const SrcLoc& loc) { return ikSolve(a[0], loc); });
    reg("audioRender", 1, [](const ValueList& a, const SrcLoc& loc) { return audioRender(a[0], loc); });
    reg("audioTrace", 1, [](const ValueList& a, const SrcLoc& loc) { return audioTrace(a[0], loc); });
    reg("audioSched", 1, [](const ValueList& a, const SrcLoc& loc) { return audioSched(a[0], loc); });
}

}
