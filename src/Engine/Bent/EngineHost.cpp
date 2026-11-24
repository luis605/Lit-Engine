#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>

#include "Engine/Bent/EngineHost.hpp"
#include "Engine/Bent/Check.hpp"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <streambuf>
#include <string>
#include <vector>

import Engine.engine;
import Engine.World;
import Engine.Physics;
import Engine.Portal;
import Engine.Streaming;
import Engine.Terrain;
import Engine.Nav;
import Engine.Geometry;
import Engine.Save;
import Engine.Delta;
import Engine.Json;
import Engine.WorldHash;
import Engine.SceneValidator;
import Engine.Replay;
import Engine.Render.settings;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.camera;
import Engine.glm;

#include "Engine/Bent/EngineSim.hpp"

namespace bent {

// ---------------------------------------------------------------- conversions

glm::vec3 toVec(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Vec3) return glm::vec3(static_cast<float>(value.v[0]), static_cast<float>(value.v[1]), static_cast<float>(value.v[2]));
    if (value.isNumber()) return glm::vec3(static_cast<float>(value.num()));
    throw BentError(loc, std::string(what) + ": expected a vec3, got " + kindName(value.kind));
}

glm::quat toQuat(const Value& value, const SrcLoc& loc, const char* what) {
    if (value.kind == Kind::Quat) return glm::quat(static_cast<float>(value.v[0]), static_cast<float>(value.v[1]), static_cast<float>(value.v[2]), static_cast<float>(value.v[3]));
    throw BentError(loc, std::string(what) + ": expected a quat, got " + kindName(value.kind));
}

Value fromVec(const glm::vec3& v) { return Value::vec3(v.x, v.y, v.z); }
Value fromDVec(const glm::dvec3& v) { return Value::vec3(v.x, v.y, v.z); }
Value fromQuat(const glm::quat& q) { return Value::quat(q.w, q.x, q.y, q.z); }

namespace {

struct ArgReader {
    const ActionV& action;
    const SrcLoc& loc;

    [[nodiscard]] const Value* get(const char* key) const { return action.arg(key); }
    [[nodiscard]] std::string what(const char* key) const { return action.name + "." + key; }
    [[nodiscard]] glm::vec3 vec(const char* key, const glm::vec3& fallback) const {
        const Value* v = get(key);
        return v ? toVec(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] glm::dvec3 dvec(const char* key, const glm::dvec3& fallback) const {
        const Value* v = get(key);
        if (!v) return fallback;
        if (v->kind != Kind::Vec3) throw BentError(loc, what(key) + ": expected a vec3");
        return glm::dvec3(v->v[0], v->v[1], v->v[2]);
    }
    [[nodiscard]] glm::quat quat(const char* key, const glm::quat& fallback) const {
        const Value* v = get(key);
        return v ? toQuat(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] double num(const char* key, double fallback) const {
        const Value* v = get(key);
        return v ? argNumber(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] float f(const char* key, float fallback) const { return static_cast<float>(num(key, fallback)); }
    [[nodiscard]] int64_t integer(const char* key, int64_t fallback) const {
        const Value* v = get(key);
        return v ? argInt(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] bool flag(const char* key, bool fallback) const {
        const Value* v = get(key);
        if (!v) return fallback;
        if (v->kind != Kind::Bool) throw BentError(loc, what(key) + ": expected a bool");
        return v->b;
    }
    [[nodiscard]] std::string str(const char* key, const std::string& fallback) const {
        const Value* v = get(key);
        if (!v) return fallback;
        if (v->kind == Kind::Str) return v->text();
        return print(*v);
    }
};

std::atomic<bool> gSilent{false};

class NullBuffer : public std::streambuf {
  protected:
    int overflow(int c) override { return c; }
    std::streamsize xsputn(const char*, std::streamsize n) override { return n; }
};

}

void EngineHost::silenceEngineLog(bool silent) {
    static NullBuffer null;
    static std::streambuf* original = std::cout.rdbuf();
    gSilent = silent;
    std::cout.rdbuf(silent ? &null : original);
}

// ---------------------------------------------------------------- action specs

const std::vector<ActionSpec>& engineActionSpecs() {
    static const std::vector<ActionSpec> specs = {
        {"spawn",
         {"shape", "pos", "size", "height", "rot", "scale", "dynamic", "mass", "restitution", "friction", "vel", "gravityScale", "mesh", "material", "name", "tag", "layer", "parent", "sleeping", "track", "light"},
         "create an entity. shape: sphere | box | capsule | hull | compound | none (size: radius or half extents, height: capsule half height). dynamic bodies get a RigidBody; static colliders an isStatic one; mesh -1 = no mesh (default 0, synthetic bounds radius 0.5 + 0.5 * (mesh % 4)); parent: spawn index or -1; track: register with the portal system"},
        {"plane", {"normal", "pos", "restitution"}, "static infinite plane collider"},
        {"move", {"target", "delta"}, "translate an entity (local)"},
        {"teleport", {"target", "pos"}, "set the local position"},
        {"setRotation", {"target", "rot"}, "set the local rotation"},
        {"setScale", {"target", "scale"}, "set the local scale"},
        {"setVelocity", {"target", "vel"}, "set a rigid body velocity (wakes it)"},
        {"impulse", {"target", "j"}, "add j / mass to the velocity (wakes it)"},
        {"force", {"target", "f", "ticks"}, "apply force f for the next `ticks` ticks (velocity += f / m * dt before each tick)"},
        {"wake", {"target"}, "wake a sleeping body"},
        {"destroy", {"target"}, "destroy an entity (stale references stay in the spawn list)"},
        {"reparent", {"target", "parent", "keepWorld"}, "set the parent (-1 detaches)"},
        {"setName", {"target", "name"}, "rename"},
        {"setTag", {"target", "tag"}, "set the tag"},
        {"setLayer", {"target", "layer"}, "set the layer mask"},
        {"setVisible", {"target", "visible"}, "show / hide"},
        {"gravity", {"g"}, "physics gravity"},
        {"solver", {"iterations", "sleepVelocity", "sleepTime"}, "physics settings"},
        {"light", {"kind", "pos", "color", "intensity", "range"}, "add a light entity (point | spot | directional)"},
        {"portalPair", {"a", "aEuler", "b", "bEuler", "half", "twist", "track"}, "add a portal pair (both ends in cell 'main', created on demand). aEuler/bEuler: yaw pitch roll degrees, any rigid transform"},
        {"camera", {"pos", "yaw", "pitch", "roll"}, "place the camera (local position, degrees)"},
        {"cameraWorld", {"pos"}, "place the camera at an absolute (double) world position (rebases when floating origin is on)"},
        {"cameraWalk", {"delta", "steps"}, "move the camera by delta in `steps` ticks, stepping portals after each move"},
        {"step", {"n"}, "advance n fixed ticks (1/60 s)"},
        {"saveLoad", {}, "World::saveScene to text, then loadScene (references remapped by slot order)"},
        {"saveRoundtrip", {}, "SaveSystem capture() then apply()"},
        {"snapshot", {"slot"}, "World::captureSnapshot into a slot"},
        {"restore", {"slot"}, "World::restoreSnapshot from a slot (no-op if empty)"},
        {"snapshotText", {"slot"}, "World::snapshot() text into a slot"},
        {"restoreText", {"slot"}, "World::restore(text, remap) from a slot"},
        {"compact", {}, "World::compact (references remapped)"},
        {"clear", {}, "World::clear"},
        {"rebase", {"delta"}, "Engine::rebaseOrigin(delta)"},
        {"floatingOrigin", {"threshold", "snap"}, "enable automatic floating origin"},
        {"streaming", {"cell", "nx", "nz", "load", "keep", "perCell"}, "grid of nx x nz streamed cells of `cell` metres from the origin (deterministic), perCell entities each"},
        {"terrain", {"tileSize", "cells", "maxLevel", "view", "amplitude"}, "enable streamed terrain with a sine height field (deterministic)"},
        {"input", {"keys", "mouse", "delta", "buttons"}, "set the input state (persists until changed; delta applies every tick)"},
        {"flyCamera", {}, "enable the fly camera system (WASD keys move the camera)"},
        {"render", {"key", "value"}, "setRenderSetting(key, value)"},
        {"deltaTrack", {"cell"}, "register every live spawned entity as a base of the delta cell and mark it resident"},
        {"deltaCapture", {"cell"}, "DeltaStore::captureCell"},
        {"deltaReload", {}, "DeltaStore toJson -> fromJson (reverts live bases to baseline)"},
        {"deltaApply", {"cell"}, "DeltaStore::applyCell"},
        {"deltaRelease", {"cell"}, "DeltaStore::releaseCell"},
        {"seed", {"n"}, "Engine::seed"},
        {"timeScale", {"s"}, "Engine::setTimeScale"},
        {"pause", {"paused"}, "Engine::setPaused"},
        {"navBuild", {"half", "cell", "radius"}, "build a nav mesh on a floor of +-half metres with the obstacles added so far"},
        {"navObstacle", {"center", "half"}, "add a nav obstacle box (rebuilds dirty tiles if a nav mesh exists)"},
        {"validate", {}, "run validateScene and World::validate (results are observed with validatorIssues etc.)"},
    };
    return specs;
}

// ---------------------------------------------------------------- sim

EngineSim::EngineSim() {
    m_engine = std::make_unique<Engine>();
    m_engine->openHeadless();
    m_engine->seed(1);
    World& w = m_engine->world();
    registerPhysicsComponents(w);
    // Synthetic mesh kit: headless has no GPU meshes, so mesh ids get deterministic bounds (origin centred sphere).
    w.setMeshBoundsHook([](uint32_t id) { return glm::vec4(0.0f, 0.0f, 0.0f, id == NO_MESH ? 0.0f : 0.5f + 0.5f * static_cast<float>(id % 4)); });
    w.camera().setPos(glm::vec3(0.0f, 2.0f, 10.0f));
}

EngineSim::~EngineSim() {
    if (m_engine && m_engine->recording()) m_engine->stopRecording();
}

void EngineSim::addRef(EntityHandle handle) {
    m_refIndex[handle] = static_cast<int64_t>(m_refs.size());
    m_refs.push_back(handle);
    m_pendingForces.emplace_back(0.0f);
    m_forceTicks.push_back(0);
}

Value EngineSim::refOf(EntityHandle handle) {
    if (handle.isNull()) return Value::integer(-1);
    const auto it = m_refIndex.find(handle);
    if (it != m_refIndex.end()) return Value::entity(it->second);
    addRef(handle);
    return Value::entity(static_cast<int64_t>(m_refs.size()) - 1);
}

EntityHandle EngineSim::resolve(const Value& target, const SrcLoc& loc, bool allowNull) const {
    int64_t index = 0;
    if (target.kind == Kind::Entity) {
        index = target.i;
        if (index < 0 || index >= static_cast<int64_t>(m_refs.size())) throw BentError(loc, "unknown entity(" + std::to_string(index) + ")");
        return m_refs[static_cast<size_t>(index)];
    }
    index = argInt(target, loc, "target");
    if (index < 0 && allowNull) return NULL_ENTITY;
    if (m_refs.empty()) return NULL_ENTITY;
    const int64_t count = static_cast<int64_t>(m_refs.size());
    if (index < 0) index = count - 1 - ((-index - 1) % count);  // -1: the most recently spawned entity
    // Random ints address spawned entities modulo their count, so every generated program is meaningful.
    return m_refs[static_cast<size_t>(index % static_cast<int64_t>(m_refs.size()))];
}

void EngineSim::remapRefs(const std::vector<EntityHandle>& before, const std::vector<EntityHandle>& after) {
    std::map<std::pair<uint32_t, uint32_t>, EntityHandle> map;
    for (size_t index = 0; index < before.size() && index < after.size(); ++index) map[{before[index].index, before[index].generation}] = after[index];
    m_refIndex.clear();
    for (size_t ref = 0; ref < m_refs.size(); ++ref) {
        const auto it = map.find({m_refs[ref].index, m_refs[ref].generation});
        m_refs[ref] = it != map.end() ? it->second : EntityHandle{INVALID_ENTITY - 1, 0xdead};
        if (it != map.end()) m_refIndex[m_refs[ref]] = static_cast<int64_t>(ref);
    }
}

namespace {
std::vector<EntityHandle> liveHandles(World& world) {
    std::vector<EntityHandle> out;
    world.forEach([&](EntityHandle h) { out.push_back(h); });
    std::sort(out.begin(), out.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
    return out;
}
}

void EngineSim::doTick() {
    World& w = world();
    for (size_t ref = 0; ref < m_refs.size(); ++ref) {
        if (m_forceTicks[ref] <= 0) continue;
        --m_forceTicks[ref];
        if (RigidBody* body = w.get<RigidBody>(m_refs[ref])) {
            if (body->mass > 0.0f) body->velocity += m_pendingForces[ref] / body->mass * kBentStep;
            wakeBody(w, m_refs[ref]);
        }
    }
    if (!m_replaying) m_engine->setInput(m_input);
    m_engine->tick(kBentStep);
    if (m_input.mouseDelta != glm::vec2(0.0f) && !m_replaying) {
        // delta persists by design of the action (applies every tick)
    }
    ++m_ticks;
}

void EngineSim::tick() { doTick(); }

void EngineSim::stepTicks(int64_t count, const std::function<bool()>& onTick) {
    for (int64_t index = 0; index < count; ++index) {
        doTick();
        if (!onTick()) return;
    }
}

void EngineSim::walkCamera(const glm::vec3& delta, int64_t steps, const std::function<bool()>& onTick) {
    Camera& camera = world().camera();
    PortalSystem& portals = m_engine->portals();
    glm::vec3 stepDelta = delta / static_cast<float>(std::max<int64_t>(steps, 1));
    for (int64_t index = 0; index < std::max<int64_t>(steps, 1); ++index) {
        camera.setPos(camera.getPosition() + stepDelta);
        if (portals.active()) {
            const glm::vec3 position = camera.getPosition();
            const glm::vec3 front = camera.getFront();
            const glm::vec3 up = camera.getUp();
            const int crossed = portals.step(camera);
            if (crossed >= 0) {
                const glm::mat4& transform = portals.portals()[static_cast<size_t>(crossed)].transform;
                const glm::vec3 expectedPos = glm::vec3(transform * glm::vec4(position, 1.0f));
                const glm::vec3 expectedFront = glm::normalize(glm::vec3(transform * glm::vec4(front, 0.0f)));
                const glm::vec3 expectedUp = glm::normalize(glm::vec3(transform * glm::vec4(up, 0.0f)));
                const double error = std::max({static_cast<double>(glm::length(camera.getPosition() - expectedPos)), static_cast<double>(glm::length(camera.getFront() - expectedFront)), static_cast<double>(glm::length(camera.getUp() - expectedUp))});
                m_portalError = std::max(m_portalError, std::isfinite(error) ? error : 1.0e30);
                ++m_crossings;
                if (m_lastCrossed >= 0 && portals.portals()[static_cast<size_t>(m_lastCrossed)].partner == crossed) ++m_pingPongs;
                m_lastCrossed = crossed;
                stepDelta = glm::vec3(transform * glm::vec4(stepDelta, 0.0f));  // keep walking straight on the far side
            } else {
                m_lastCrossed = -1;
            }
        }
        doTick();
        if (!onTick()) return;
    }
}

void EngineSim::startRecording(const std::string& path) { m_engine->startRecording(path); }
void EngineSim::stopRecording() {
    if (m_engine->recording()) m_engine->stopRecording();
}
bool EngineSim::startReplay(const std::string& path) {
    m_replaying = m_engine->startReplay(path);
    if (m_replaying) m_engine->seed(m_engine->replaySeed());
    return m_replaying;
}

uint64_t EngineSim::replayHash() {
    uint64_t h = hashWorldState(world());
    const glm::vec3 p = world().camera().getPosition();
    StateHasher hasher;
    hasher.addU64(h);
    hasher.addVec3(p.x, p.y, p.z);
    hasher.addFloat(world().camera().getYaw());
    hasher.addFloat(world().camera().getPitch());
    return hasher.value();
}

namespace {

std::shared_ptr<const ConvexHullData> cubeHull(const glm::vec3& half) {
    std::vector<glm::vec3> points;
    for (int corner = 0; corner < 8; ++corner) points.emplace_back((corner & 1) ? half.x : -half.x, (corner & 2) ? half.y : -half.y, (corner & 4) ? half.z : -half.z);
    return makeConvexHull(points);
}

float terrainSine(float x, float z, float amplitude) { return amplitude * (std::sin(x * 0.013f) * std::cos(z * 0.011f) + 0.3f * std::sin((x + z) * 0.05f)); }

}

void EngineSim::apply(const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick) {
    const ArgReader a{action, loc};
    World& w = world();
    const std::string& name = action.name;
    if (name == "step") {
        const int64_t n = a.integer("n", 1);
        if (n < 0 || n > 1000000) throw BentError(loc, "step(n) needs 0 <= n <= 1000000");
        stepTicks(n, onTick);
        return;
    }
    if (name == "spawn") {
        const std::string shape = a.str("shape", "sphere");
        EntityDesc desc;
        desc.name = a.str("name", "e" + std::to_string(m_refs.size()));
        const int64_t mesh = a.integer("mesh", 0);
        desc.mesh = mesh < 0 ? NO_MESH : static_cast<uint32_t>(mesh);
        desc.material = static_cast<uint32_t>(std::max<int64_t>(0, a.integer("material", 0)));
        desc.position = a.vec("pos", glm::vec3(0.0f, 1.0f, 0.0f));
        desc.rotation = a.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        desc.scale = a.vec("scale", glm::vec3(1.0f));
        const int64_t parent = a.integer("parent", -1);
        if (parent >= 0 && !m_refs.empty()) {
            const EntityHandle p = m_refs[static_cast<size_t>(parent % static_cast<int64_t>(m_refs.size()))];
            if (w.isAlive(p)) desc.parent = p;
        }
        const EntityHandle h = w.create(desc);
        addRef(h);
        if (h.isNull()) return;
        if (const Value* tag = a.get("tag")) w.setTag(h, tag->kind == Kind::Str ? tag->text() : print(*tag));
        if (a.get("layer")) w.setLayer(h, static_cast<uint32_t>(a.integer("layer", 1)));
        const Value* sizeValue = a.get("size");
        const glm::vec3 half = sizeValue && sizeValue->kind == Kind::Vec3 ? toVec(*sizeValue, loc, "spawn.size") : glm::vec3(a.f("size", 0.5f));
        const float radius = sizeValue && sizeValue->kind == Kind::Vec3 ? half.x : a.f("size", 0.5f);
        if (shape == "sphere") w.add<SphereCollider>(h, SphereCollider{radius});
        else if (shape == "box") w.add<BoxCollider>(h, BoxCollider{half});
        else if (shape == "capsule") w.add<CapsuleCollider>(h, CapsuleCollider{radius, a.f("height", 0.5f)});
        else if (shape == "hull") {
            if (auto hull = cubeHull(half)) w.add<ConvexHullCollider>(h, ConvexHullCollider{hull});
        } else if (shape == "compound") {
            CompoundCollider compound;
            CompoundPart left;
            left.kind = CompoundPart::Kind::Sphere;
            left.offset = glm::vec3(-radius, 0.0f, 0.0f);
            left.radius = radius * 0.75f;
            CompoundPart right = left;
            right.kind = CompoundPart::Kind::Box;
            right.offset = glm::vec3(radius, 0.0f, 0.0f);
            right.halfExtents = glm::vec3(radius * 0.5f);
            compound.parts = {left, right};
            w.add<CompoundCollider>(h, std::move(compound));
        } else if (shape != "none") {
            throw BentError(loc, "spawn.shape: unknown shape '" + shape + "' (sphere, box, capsule, hull, compound, none)");
        }
        const bool dynamic = a.flag("dynamic", true);
        if (shape != "none" || dynamic) {
            RigidBody body;
            body.isStatic = !dynamic;
            body.mass = a.f("mass", 1.0f);
            body.restitution = a.f("restitution", 0.0f);
            body.friction = a.f("friction", 0.5f);
            body.velocity = a.vec("vel", glm::vec3(0.0f));
            body.gravityScale = a.f("gravityScale", 1.0f);
            body.sleeping = a.flag("sleeping", false);
            w.add<RigidBody>(h, body);
        }
        if (a.flag("light", false)) w.add<LightComponent>(h);
        if (a.flag("track", false) && m_engine->portals().active()) m_engine->portals().track(w, h);
        return;
    }
    if (name == "plane") {
        EntityDesc desc;
        desc.name = "plane" + std::to_string(m_refs.size());
        desc.mesh = NO_MESH;
        desc.position = a.vec("pos", glm::vec3(0.0f));
        const EntityHandle h = w.create(desc);
        addRef(h);
        PlaneCollider plane;
        plane.normal = a.vec("normal", glm::vec3(0.0f, 1.0f, 0.0f));
        plane.restitution = a.f("restitution", 0.0f);
        w.add<PlaneCollider>(h, plane);
        return;
    }
    const auto target = [&]() { return resolve(a.get("target") ? *a.get("target") : Value::integer(0), loc); };
    if (name == "move") return w.translate(target(), a.vec("delta", glm::vec3(0.0f)));
    if (name == "teleport") return w.setPosition(target(), a.vec("pos", glm::vec3(0.0f)));
    if (name == "setRotation") return w.setRotation(target(), a.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
    if (name == "setScale") return w.setScale(target(), a.vec("scale", glm::vec3(1.0f)));
    if (name == "setName") return w.setName(target(), a.str("name", ""));
    if (name == "setTag") return w.setTag(target(), a.str("tag", ""));
    if (name == "setLayer") return w.setLayer(target(), static_cast<uint32_t>(a.integer("layer", 1)));
    if (name == "setVisible") return w.setVisible(target(), a.flag("visible", true));
    if (name == "destroy") return w.destroy(target());
    if (name == "wake") return wakeBody(w, target());
    if (name == "setVelocity" || name == "impulse") {
        const EntityHandle h = target();
        if (RigidBody* body = w.get<RigidBody>(h)) {
            if (name == "setVelocity") body->velocity = a.vec("vel", glm::vec3(0.0f));
            else if (body->mass > 0.0f) body->velocity += a.vec("j", glm::vec3(0.0f)) / body->mass;
            wakeBody(w, h);
            ++m_impulses;
        }
        return;
    }
    if (name == "force") {
        if (m_refs.empty()) return;
        const EntityHandle h = target();
        const auto it = m_refIndex.find(h);
        if (it == m_refIndex.end()) return;
        m_pendingForces[static_cast<size_t>(it->second)] = a.vec("f", glm::vec3(0.0f));
        m_forceTicks[static_cast<size_t>(it->second)] = std::clamp<int64_t>(a.integer("ticks", 1), 0, 100000);
        ++m_impulses;
        return;
    }
    if (name == "reparent") {
        const EntityHandle child = target();
        const EntityHandle parent = resolve(a.get("parent") ? *a.get("parent") : Value::integer(-1), loc, true);
        return w.setParent(child, parent, a.flag("keepWorld", true));
    }
    if (name == "gravity") {
        m_engine->physics().gravity = a.vec("g", glm::vec3(0.0f, -9.81f, 0.0f));
        return;
    }
    if (name == "solver") {
        PhysicsSettings& physics = m_engine->physics();
        physics.solverIterations = static_cast<int>(std::clamp<int64_t>(a.integer("iterations", physics.solverIterations), 0, 64));
        physics.sleepVelocity = a.f("sleepVelocity", physics.sleepVelocity);
        physics.sleepTime = a.f("sleepTime", physics.sleepTime);
        return;
    }
    if (name == "light") {
        EntityDesc desc;
        desc.name = "light" + std::to_string(m_refs.size());
        desc.mesh = NO_MESH;
        desc.position = a.vec("pos", glm::vec3(0.0f, 5.0f, 0.0f));
        const EntityHandle h = w.create(desc);
        addRef(h);
        LightComponent light;
        const std::string kind = a.str("kind", "point");
        light.type = kind == "spot" ? LightComponent::Type::Spot : (kind == "directional" ? LightComponent::Type::Directional : LightComponent::Type::Point);
        light.color = a.vec("color", glm::vec3(1.0f));
        light.intensity = a.f("intensity", 1.0e6f);
        light.range = a.f("range", 100.0f);
        w.add<LightComponent>(h, light);
        return;
    }
    if (name == "portalPair") {
        PortalSystem& portals = m_engine->portals();
        if (portals.findCell("main") < 0) {
            PortalCell cell;
            cell.name = "main";
            portals.addCell(cell);
        }
        PortalEnd endA;
        endA.cell = "main";
        endA.position = a.vec("a", glm::vec3(0.0f, 1.5f, -5.0f));
        endA.eulerDegrees = a.vec("aEuler", glm::vec3(0.0f));
        const glm::vec3 half = a.vec("half", glm::vec3(1.5f, 1.5f, 0.0f));
        endA.halfSize = glm::vec2(half.x, half.y);
        PortalEnd endB = endA;
        endB.position = a.vec("b", glm::vec3(20.0f, 1.5f, -5.0f));
        endB.eulerDegrees = a.vec("bEuler", glm::vec3(0.0f, 180.0f, 0.0f));
        portals.addPair("pair" + std::to_string(portals.portals().size() / 2), endA, endB, a.f("twist", 0.0f));
        return;
    }
    if (name == "camera") {
        Camera& camera = w.camera();
        camera.setPos(a.vec("pos", glm::vec3(0.0f, 2.0f, 10.0f)));
        camera.setOrientation(a.f("yaw", -90.0f), a.f("pitch", 0.0f), a.f("roll", 0.0f));
        return;
    }
    if (name == "cameraWorld") return m_engine->setCameraWorldPosition(a.dvec("pos", glm::dvec3(0.0, 2.0, 10.0)));
    if (name == "cameraWalk") {
        const int64_t steps = a.integer("steps", 10);
        if (steps < 1 || steps > 100000) throw BentError(loc, "cameraWalk.steps must be in 1..100000");
        walkCamera(a.vec("delta", glm::vec3(0.0f, 0.0f, -1.0f)), steps, onTick);
        return;
    }
    if (name == "saveLoad") {
        const std::vector<EntityHandle> before = liveHandles(w);
        std::stringstream text;
        if (!w.saveScene(text)) throw BentError(loc, "saveScene failed");
        std::istringstream in(text.str());
        if (!w.loadScene(in)) throw BentError(loc, "loadScene rejected the scene it just saved");
        remapRefs(before, liveHandles(w));
        return;
    }
    if (name == "saveRoundtrip") {
        SaveSystem& save = m_engine->save() ? *m_engine->save() : m_engine->enableSave();
        const std::vector<EntityHandle> before = liveHandles(w);
        const Json body = save.capture();
        std::string error;
        if (!save.apply(body, &error)) throw BentError(loc, "SaveSystem::apply rejected its own capture: " + error);
        std::vector<EntityHandle> after;
        for (const EntityHandle h : before) after.push_back(save.remap(h));
        remapRefs(before, after);
        return;
    }
    if (name == "snapshot") {
        m_snapshots[a.integer("slot", 0)] = w.captureSnapshot();
        return;
    }
    if (name == "restore") {
        const auto it = m_snapshots.find(a.integer("slot", 0));
        if (it != m_snapshots.end()) w.restoreSnapshot(it->second);
        return;
    }
    if (name == "snapshotText") {
        m_textSnapshots[a.integer("slot", 0)] = w.snapshot();
        return;
    }
    if (name == "restoreText") {
        const auto it = m_textSnapshots.find(a.integer("slot", 0));
        if (it == m_textSnapshots.end()) return;
        const std::vector<EntityHandle> before = liveHandles(w);
        std::vector<EntityHandle> remap;
        if (!w.restore(it->second, &remap)) throw BentError(loc, "World::restore rejected its own snapshot text");
        std::vector<EntityHandle> after;
        for (const EntityHandle h : before) after.push_back(h.index < remap.size() ? remap[h.index] : NULL_ENTITY);
        // Entities created after the snapshot are gone; references to older ones follow the remap of the snapshot's slots.
        std::map<std::pair<uint32_t, uint32_t>, EntityHandle> map;
        m_refIndex.clear();
        for (size_t ref = 0; ref < m_refs.size(); ++ref) {
            const EntityHandle old = m_refs[ref];
            EntityHandle mapped = old.index < remap.size() ? remap[old.index] : NULL_ENTITY;
            if (!w.isAlive(mapped)) mapped = EntityHandle{INVALID_ENTITY - 1, 0xdead};
            m_refs[ref] = mapped;
            if (w.isAlive(mapped)) m_refIndex[mapped] = static_cast<int64_t>(ref);
        }
        return;
    }
    if (name == "compact") {
        const std::vector<EntityMoved> moves = w.compact();
        std::map<std::pair<uint32_t, uint32_t>, EntityHandle> map;
        for (const EntityMoved& move : moves) map[{move.from.index, move.from.generation}] = move.to;
        m_refIndex.clear();
        for (size_t ref = 0; ref < m_refs.size(); ++ref) {
            const auto it = map.find({m_refs[ref].index, m_refs[ref].generation});
            if (it != map.end()) m_refs[ref] = it->second;
            if (w.isAlive(m_refs[ref])) m_refIndex[m_refs[ref]] = static_cast<int64_t>(ref);
        }
        return;
    }
    if (name == "clear") return w.clear();
    if (name == "rebase") {
        m_engine->rebaseOrigin(a.dvec("delta", glm::dvec3(0.0)));
        return;
    }
    if (name == "floatingOrigin") {
        FloatingOriginConfig config;
        config.threshold = a.num("threshold", 2000.0);
        config.snap = a.num("snap", 1024.0);
        m_engine->enableFloatingOrigin(config);
        return;
    }
    if (name == "streaming") {
        if (m_grid.enabled) return;
        m_grid.enabled = true;
        m_grid.cellSize = std::clamp(a.f("cell", 100.0f), 1.0f, 10000.0f);
        m_grid.nx = static_cast<int>(std::clamp<int64_t>(a.integer("nx", 6), 1, 64));
        m_grid.nz = static_cast<int>(std::clamp<int64_t>(a.integer("nz", 6), 1, 64));
        m_grid.loadRadius = a.f("load", 150.0f);
        m_grid.keepRadius = std::max(m_grid.loadRadius, a.f("keep", 200.0f));
        m_grid.perCell = static_cast<int>(std::clamp<int64_t>(a.integer("perCell", 3), 0, 64));
        m_grid.first = addGridCells(m_engine->portals(), w, glm::dvec3(0.0), m_grid.cellSize, m_grid.nx, m_grid.nz);
        StreamingConfig config;
        config.spatial.enabled = true;
        config.spatial.loadRadius = m_grid.loadRadius;
        config.spatial.keepRadius = m_grid.keepRadius;
        config.deterministic = true;
        config.uploadBytesPerFrame = 1u << 30;
        config.commitMillisPerFrame = 1000.0;
        StreamingSystem& streaming = m_engine->enableStreaming(config);
        const StreamGrid grid = m_grid;
        streaming.setLoader(makeBatchLoader(w, m_engine->portals(), nullptr, [grid](int cell) {
            CellContent content;
            const int local = cell - grid.first;
            const int cx = local / grid.nz;
            const int cz = local % grid.nz;
            for (int index = 0; index < grid.perCell; ++index) {
                CellEntity e;
                e.key = "k" + std::to_string(index);
                e.desc.name = "s" + std::to_string(cell) + "_" + std::to_string(index);
                e.desc.mesh = 0;
                e.hasWorldPosition = true;
                const double fx = (static_cast<double>(index) + 0.5) / static_cast<double>(grid.perCell);
                e.worldPosition = glm::dvec3((cx + fx) * grid.cellSize, 0.5, (cz + 0.5) * grid.cellSize);
                content.entities.push_back(e);
            }
            return content;
        }));
        return;
    }
    if (name == "terrain") {
        if (m_terrain) return;
        m_terrain = true;
        TerrainSetup setup;
        const float amplitude = a.f("amplitude", 20.0f);
        setup.terrain.height = [amplitude](float x, float z) { return terrainSine(x, z, amplitude); };
        setup.terrain.tileSize = std::clamp(a.f("tileSize", 64.0f), 4.0f, 4096.0f);
        setup.terrain.tileCells = static_cast<int>(std::clamp<int64_t>(a.integer("cells", 16), 2, 128));
        setup.terrain.minHeight = -amplitude * 2.0f - 1.0f;
        setup.terrain.maxHeight = amplitude * 2.0f + 1.0f;
        setup.stream.lod.maxLevel = static_cast<int>(std::clamp<int64_t>(a.integer("maxLevel", 3), 0, 8));
        setup.stream.lod.viewDistance = a.f("view", 500.0f);
        setup.stream.deterministic = true;
        setup.stream.workers = 1;
        setup.colliderMaxLevel = 0;
        setup.scatterMaxLevel = -1;
        setup.prime = true;
        m_engine->enableTerrain(setup);
        return;
    }
    if (name == "input") {
        InputState state;
        if (const Value* keys = a.get("keys")) {
            for (const Value& key : argList(*keys, loc, "input.keys")) {
                const int64_t code = argInt(key, loc, "input key");
                if (code >= 0 && code < static_cast<int64_t>(state.keys.size())) state.keys.set(static_cast<size_t>(code));
            }
        }
        const glm::vec3 mouse = a.vec("mouse", glm::vec3(0.0f));
        state.mouse = glm::vec2(mouse.x, mouse.y);
        const glm::vec3 delta = a.vec("delta", glm::vec3(0.0f));
        state.mouseDelta = glm::vec2(delta.x, delta.y);
        const int64_t buttons = a.integer("buttons", 0);
        for (size_t bit = 0; bit < state.buttons.size(); ++bit) state.buttons.set(bit, (buttons >> bit) & 1);
        m_input = state;
        return;
    }
    if (name == "flyCamera") {
        if (!m_flyCamera) m_engine->enableFlyCamera();
        m_flyCamera = true;
        return;
    }
    if (name == "render") {
        const std::string key = a.str("key", "exposure");
        const Value* value = a.get("value");
        const std::string text = !value ? std::string("0") : (value->kind == Kind::Str ? value->text() : (value->kind == Kind::Bool ? (value->b ? "1" : "0") : print(*value)));
        setRenderSetting(m_engine->renderSettings(), key, text);
        return;
    }
    if (name == "deltaTrack" || name == "deltaCapture" || name == "deltaApply" || name == "deltaRelease" || name == "deltaReload") {
        DeltaStore& deltas = m_engine->deltas() ? *m_engine->deltas() : m_engine->enableDeltas();
        const std::string cell = a.str("cell", "c");
        if (name == "deltaTrack") {
            for (size_t ref = 0; ref < m_refs.size(); ++ref) {
                if (w.isAlive(m_refs[ref])) deltas.registerBase(cell, "e" + std::to_string(ref), m_refs[ref]);
            }
            deltas.applyCell(cell);
        } else if (name == "deltaCapture") {
            deltas.captureCell(cell);
        } else if (name == "deltaApply") {
            deltas.applyCell(cell);
        } else if (name == "deltaRelease") {
            deltas.releaseCell(cell);
        } else {
            const Json doc = deltas.toJson();
            const std::string text = doc.dump();
            std::string error;
            const std::optional<Json> parsed = Json::parse(text, &error);
            if (!parsed) throw BentError(loc, "delta JSON does not parse back: " + error);
            if (!deltas.fromJson(*parsed, &error)) throw BentError(loc, "DeltaStore::fromJson rejected its own toJson: " + error);
        }
        return;
    }
    if (name == "seed") return m_engine->seed(static_cast<uint64_t>(a.integer("n", 1)));
    if (name == "timeScale") return m_engine->setTimeScale(a.f("s", 1.0f));
    if (name == "pause") return m_engine->setPaused(a.flag("paused", true));
    if (name == "navObstacle") {
        const glm::vec3 center = a.vec("center", glm::vec3(0.0f));
        const glm::vec3 half = glm::abs(a.vec("half", glm::vec3(1.0f)));
        Aabb box;
        box.grow(center - half);
        box.grow(center + half);
        m_nav.obstacles.push_back(box);
        if (m_nav.mesh) {
            m_nav.mesh->addObstacle(box);
            m_nav.mesh->rebuildDirty();
        }
        return;
    }
    if (name == "navBuild") {
        m_nav.half = std::clamp(a.f("half", 20.0f), 1.0f, 500.0f);
        m_nav.cell = std::clamp(a.f("cell", 0.5f), 0.05f, 10.0f);
        m_nav.radius = std::clamp(a.f("radius", 0.4f), 0.0f, 10.0f);
        TriangleSoup soup;
        soup.addBox(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.5f, 0.0f)), glm::vec3(m_nav.half, 0.5f, m_nav.half));
        m_nav.mesh = std::make_unique<NavMesh>();
        NavConfig config;
        config.originX = -m_nav.half;
        config.originZ = -m_nav.half;
        config.cellSize = m_nav.cell;
        config.agentRadius = m_nav.radius;
        m_nav.mesh->configure(config);
        m_nav.mesh->setGeometry(soup);
        for (const Aabb& box : m_nav.obstacles) m_nav.mesh->addObstacle(box);
        m_nav.mesh->rebuildAll();
        return;
    }
    if (name == "validate") {
        ValidatorLimits limits;
        limits.portals = &m_engine->portals();
        (void)validateScene(w, limits);
        (void)w.validate();
        return;
    }
    throw BentError(loc, "unknown action '" + name + "'");
}

// ---------------------------------------------------------------- host

EngineHost::EngineHost() = default;
EngineHost::~EngineHost() = default;

std::unique_ptr<Sim> EngineHost::createSim() { return std::make_unique<EngineSim>(); }

const std::vector<ActionSpec>& EngineHost::actions() const { return engineActionSpecs(); }

const std::vector<ObservationSpec>& EngineHost::observations() const { return engineObservationSpecs(); }

}
