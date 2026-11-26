#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "Engine/Bent/EngineHost.hpp"
#include "Engine/Bent/Check.hpp"

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
import Engine.Jobs;
import Engine.Params;
import Engine.Rng;
import Engine.GizmoMath;

#include "Engine/Bent/EngineSim.hpp"
#include "Engine/Bent/Ext_World.hpp"

namespace bent {

namespace {

struct BentTestEvent {
    int64_t value = 0;
};

struct ExtWorldState {
    std::map<int64_t, Prefab> prefabs;
    std::map<int64_t, uint32_t> timerIds;
    std::vector<std::pair<int64_t, int64_t>> timerLog;
    std::vector<bool> cancelResults;
    std::map<int64_t, uint32_t> busSubs;
    std::vector<std::pair<int64_t, int64_t>> busLog;
    std::vector<std::array<int64_t, 4>> entityEvents;
    std::vector<std::pair<std::string, uint32_t>> rngLog;
    std::map<int64_t, std::set<std::pair<uint32_t, uint32_t>>> textSnapshotAlive;
    std::unique_ptr<JobSystem> jobs;
    bool loadOk = true;
    bool watching = false;
};

ExtWorldState& stateOf(EngineSim& sim) {
    if (!sim.extWorld) sim.extWorld = std::make_shared<ExtWorldState>();
    return *static_cast<ExtWorldState*>(sim.extWorld.get());
}

struct Args {
    const ActionV& action;
    const SrcLoc& loc;
    [[nodiscard]] const Value* get(const char* key) const { return action.arg(key); }
    [[nodiscard]] std::string what(const char* key) const { return action.name + "." + key; }
    [[nodiscard]] glm::vec3 vec(const char* key, const glm::vec3& fallback) const {
        const Value* v = get(key);
        return v ? toVec(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] glm::quat quat(const char* key, const glm::quat& fallback) const {
        const Value* v = get(key);
        return v ? toQuat(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] double num(const char* key, double fallback) const {
        const Value* v = get(key);
        return v ? argNumber(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] int64_t integer(const char* key, int64_t fallback) const {
        const Value* v = get(key);
        return v ? argInt(*v, loc, what(key).c_str()) : fallback;
    }
    [[nodiscard]] std::string str(const char* key, const std::string& fallback) const {
        const Value* v = get(key);
        if (!v) return fallback;
        return v->kind == Kind::Str ? v->text() : print(*v);
    }
};

Value matrixValue(const glm::mat4& m) {
    ValueList out;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) out.push_back(Value::number(m[c][r]));
    }
    return Value::makeList(std::move(out));
}

std::vector<EntityHandle> liveSorted(World& world) {
    std::vector<EntityHandle> out;
    world.forEach([&](EntityHandle h) { out.push_back(h); });
    std::sort(out.begin(), out.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
    return out;
}

Value entityList(EngineSim& sim, std::vector<EntityHandle> handles) {
    std::sort(handles.begin(), handles.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
    ValueList out;
    for (const EntityHandle h : handles) out.push_back(sim.refOf(h));
    return Value::makeList(std::move(out));
}

glm::mat4 chainMatrix(World& w, EntityHandle h) {
    std::vector<EntityHandle> chain;
    for (EntityHandle cur = h; !cur.isNull() && w.isAlive(cur) && chain.size() < 100000; cur = w.getParent(cur)) chain.push_back(cur);
    glm::mat4 m(1.0f);
    for (size_t k = chain.size(); k-- > 0;) m = m * w.getLocalMatrix(chain[k]);
    return m;
}

uint64_t subtreeHash(World& w, EntityHandle h, int depth) {
    StateHasher hasher;
    hasher.addText(w.getName(h));
    hasher.addText(w.getTag(h));
    hasher.addU64(w.getLayer(h));
    hasher.addU64(w.getMesh(h));
    hasher.addBool(w.isVisible(h));
    const glm::mat4& m = w.getLocalMatrix(h);
    for (int c = 0; c < 4; ++c) hasher.addVec3(m[c][0], m[c][1], m[c][2]);
    for (const std::string& component : w.componentNames()) {
        if (const std::optional<std::string> text = w.componentText(h, component)) {
            hasher.addText(component);
            hasher.addText(*text);
        }
    }
    std::vector<uint64_t> kids;
    if (depth < 10000) {
        for (const EntityHandle c : w.getChildren(h)) kids.push_back(subtreeHash(w, c, depth + 1));
    }
    std::sort(kids.begin(), kids.end());
    hasher.addU64(kids.size());
    for (const uint64_t k : kids) hasher.addU64(k);
    return hasher.value();
}

int64_t asInt64(uint64_t v) { return static_cast<int64_t>(v); }

const std::vector<ActionSpec> kActions = {
    {"setWorldPosition", {"target", "pos"}, "World::setWorldPosition (through the parent chain)"},
    {"setWorldRotation", {"target", "rot"}, "World::setWorldRotation"},
    {"lookAtTarget", {"target", "at", "up"}, "World::lookAt a world point"},
    {"rotateBy", {"target", "axis", "degrees"}, "World::rotate about a (local) axis"},
    {"addComponent", {"target", "name"}, "World::addComponentByName"},
    {"removeComponent", {"target", "name"}, "World::removeComponentByName"},
    {"setComponent", {"target", "name", "text"}, "World::setComponentFromText"},
    {"prefabCapture", {"target", "slot"}, "World::capture the subtree into a slot"},
    {"prefabInstantiate", {"slot", "parent"}, "World::instantiate a captured prefab (new entities become referable)"},
    {"timerAfter", {"id", "seconds", "owner"}, "World::after: logs (id, tick) when it fires; owner = spawn index or -1"},
    {"timerEvery", {"id", "seconds", "owner"}, "World::every: logs (id, tick) every time it fires"},
    {"timerCancel", {"id"}, "World::cancel (result in timerCancelResults())"},
    {"busSubscribe", {"id", "mode"}, "subscribe to the test event on World::events(). mode 0 plain, 1 unsubscribes itself on its first event, 2 re-emits value + 1000 when value < 1000"},
    {"busUnsubscribe", {"id"}, "EventBus::unsubscribe"},
    {"busEmit", {"value"}, "EventBus::emit (queued)"},
    {"busEmitNow", {"value"}, "EventBus::emitNow"},
    {"busDispatch", {}, "EventBus::dispatch"},
    {"eventsWatch", {}, "log EntityCreated / EntityDestroyed / EntityReparented / EntityMoved (see entityEvents())"},
    {"jobWorkers", {"n"}, "give the world a JobSystem with n workers (parallel spatial refresh, scene parse)"},
    {"rngDraw", {"stream", "n"}, "draw n values from the world's named random stream (see rngLog())"},
    {"loadSceneText", {"text"}, "World::loadScene from text (result in loadOk())"},
    {"sceneMutate", {"mode", "at", "char"}, "save the scene, mutate the text (0 insert, 1 delete, 2 replace, 3 truncate, 4 swap lines, 5 header version = at, 6 append unknown lines) and load it back (result in loadOk())"},
    {"spawnBatch", {"n", "pos", "spacing", "mesh"}, "World::createBatch of n unparented entities in a row"},
    {"teleportAll", {"delta"}, "World::setPositions: every entity moves by delta * (1 + index % 3) (bulk path above 4096 entities)"},
    {"teleportEach", {"delta"}, "same as teleportAll through single setPosition calls"},
    {"destroyBatch", {"targets", "each"}, "World::destroyBatch of a list of spawn indices (each: 1 = destroy one by one instead)"},
};

const std::vector<ObservationSpec> kObservations = {
    {"worldMatrix", "worldMatrix(e)", "cached world matrix (16 floats, column major)"},
    {"worldMatrixRef", "worldMatrixRef(e)", "world matrix recomputed from the local matrices of the parent chain"},
    {"worldRotation", "worldRotation(e)", "World::getWorldRotation"},
    {"forwardOf", "forwardOf(e)", "World::forward"},
    {"layerOf", "layerOf(e)", "layer mask"},
    {"inLayer", "inLayer(mask)", "World::forEachInLayer, entities in index order"},
    {"findByTag", "findByTag(tag)", "World::findByTag, entities in index order"},
    {"findAllNamed", "findAllNamed(name)", "World::findAll, entities in index order"},
    {"findNamed", "findNamed(name)", "World::find (an entity or -1)"},
    {"componentNames", "componentNames()", "registered component names"},
    {"componentText", "componentText(e, name)", "serialised component or unit"},
    {"timerCount", "timerCount()", "World::timerCount"},
    {"timerLog", "timerLog()", "[id, tick] of every timer callback so far"},
    {"timerCancelResults", "timerCancelResults()", "results of timerCancel actions"},
    {"busLog", "busLog()", "[subscriber, value] of every delivery"},
    {"busPending", "busPending()", "EventBus::pending"},
    {"entityEvents", "entityEvents()", "[kind, index, generation, aux] (kinds: 0 created, 1 destroyed, 2 reparented (aux: new parent index or -1), 3 moved (aux: new index))"},
    {"rngLog", "rngLog()", "[stream, value] of every rngDraw"},
    {"loadOk", "loadOk()", "result of the last loadSceneText / sceneMutate"},
    {"subtreeHash", "subtreeHash(e)", "slot independent hash of the subtree under e (local transforms, names, tags, components)"},
    {"snapshotString", "snapshotString()", "World::snapshot() text"},
    {"toLocalPoint", "toLocalPoint(v)", "World::toLocal"},
    {"toWorldPoint", "toWorldPoint(v)", "World::toWorld"},
    {"frustumQuery", "frustumQuery()", "World::queryFrustum(camera), entities in index order"},
    {"frustumBrute", "frustumBrute(slack)", "brute force: every visible entity whose bounding sphere touches the camera frustum planes"},
    {"sceneBoundsOf", "sceneBoundsOf()", "World::sceneBounds as {has, center, radius}"},
    {"screenRayOf", "screenRayOf(x, y, w, h)", "World::screenRay as {origin, direction}"},
    {"jobWorkerCount", "jobWorkerCount()", "workers of the JobSystem given by jobWorkers (0 when none)"},
};

}

const std::vector<ActionSpec>& extWorldActionSpecs() { return kActions; }
const std::vector<ObservationSpec>& extWorldObservationSpecs() { return kObservations; }

void extWorldRecordTextSnapshot(EngineSim& sim, int64_t slot) {
    auto& alive = stateOf(sim).textSnapshotAlive[slot];
    alive.clear();
    sim.world().forEach([&](EntityHandle h) { alive.emplace(h.index, h.generation); });
}

bool extWorldAliveAtTextSnapshot(EngineSim& sim, int64_t slot, uint32_t index, uint32_t generation) {
    const auto& all = stateOf(sim).textSnapshotAlive;
    const auto it = all.find(slot);
    return it != all.end() && it->second.contains({index, generation});
}

bool extWorldApply(EngineSim& sim, const ActionV& action, const SrcLoc& loc, const std::function<bool()>& onTick) {
    (void)onTick;
    const std::string& name = action.name;
    const bool mine = std::any_of(kActions.begin(), kActions.end(), [&](const ActionSpec& s) { return s.name == name; });
    if (!mine) return false;
    const Args a{action, loc};
    World& w = sim.world();
    ExtWorldState& st = stateOf(sim);
    const auto target = [&]() { return sim.resolve(a.get("target") ? *a.get("target") : Value::integer(0), loc); };

    if (name == "setWorldPosition") return w.setWorldPosition(target(), a.vec("pos", glm::vec3(0.0f))), true;
    if (name == "setWorldRotation") return w.setWorldRotation(target(), a.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f))), true;
    if (name == "lookAtTarget") return w.lookAt(target(), a.vec("at", glm::vec3(0.0f, 0.0f, -1.0f)), a.vec("up", glm::vec3(0.0f, 1.0f, 0.0f))), true;
    if (name == "rotateBy") return w.rotate(target(), a.vec("axis", glm::vec3(0.0f, 1.0f, 0.0f)), glm::radians(static_cast<float>(a.num("degrees", 0.0)))), true;
    if (name == "addComponent") return (void)w.addComponentByName(target(), a.str("name", "")), true;
    if (name == "removeComponent") return (void)w.removeComponentByName(target(), a.str("name", "")), true;
    if (name == "setComponent") return (void)w.setComponentFromText(target(), a.str("name", ""), a.str("text", "")), true;
    if (name == "prefabCapture") {
        st.prefabs[a.integer("slot", 0)] = w.capture(target());
        return true;
    }
    if (name == "prefabInstantiate") {
        const auto it = st.prefabs.find(a.integer("slot", 0));
        if (it == st.prefabs.end()) return true;
        const EntityHandle parent = a.get("parent") ? sim.resolve(*a.get("parent"), loc, true) : NULL_ENTITY;
        std::vector<EntityHandle> created;
        w.instantiate(it->second, w.isAlive(parent) ? parent : NULL_ENTITY, &created);
        for (const EntityHandle h : created) (void)sim.refOf(h);
        return true;
    }
    if (name == "timerAfter" || name == "timerEvery") {
        const int64_t id = a.integer("id", 0);
        const EntityHandle owner = a.get("owner") ? sim.resolve(*a.get("owner"), loc, true) : NULL_ENTITY;
        ExtWorldState* state = &st;
        EngineSim* simPtr = &sim;
        const World::TimerFn fn = [state, simPtr, id](World&, EntityHandle) { state->timerLog.emplace_back(id, static_cast<int64_t>(simPtr->ticks()) + 1); };
        const float seconds = static_cast<float>(a.num("seconds", 1.0));
        st.timerIds[id] = name == "timerAfter" ? w.after(seconds, fn, owner) : w.every(seconds, fn, owner);
        return true;
    }
    if (name == "timerCancel") {
        const auto it = st.timerIds.find(a.integer("id", 0));
        st.cancelResults.push_back(it != st.timerIds.end() && w.cancel(it->second));
        return true;
    }
    if (name == "busSubscribe") {
        const int64_t id = a.integer("id", 0);
        const int64_t mode = a.integer("mode", 0);
        ExtWorldState* state = &st;
        World* world = &w;
        auto handle = std::make_shared<uint32_t>(0);
        *handle = w.events().subscribe<BentTestEvent>([state, world, id, mode, handle](const BentTestEvent& event) {
            state->busLog.emplace_back(id, event.value);
            if (mode == 1) world->events().unsubscribe(*handle);
            if (mode == 2 && event.value < 1000) world->events().emit(BentTestEvent{event.value + 1000});
        });
        if (const auto old = st.busSubs.find(id); old != st.busSubs.end()) (void)old;
        st.busSubs[id] = *handle;
        return true;
    }
    if (name == "busUnsubscribe") {
        const auto it = st.busSubs.find(a.integer("id", 0));
        if (it != st.busSubs.end()) w.events().unsubscribe(it->second);
        return true;
    }
    if (name == "busEmit") return w.events().emit(BentTestEvent{a.integer("value", 0)}), true;
    if (name == "busEmitNow") return w.events().emitNow(BentTestEvent{a.integer("value", 0)}), true;
    if (name == "busDispatch") return w.events().dispatch(), true;
    if (name == "eventsWatch") {
        if (st.watching) return true;
        st.watching = true;
        ExtWorldState* state = &st;
        w.events().subscribe<EntityCreated>([state](const EntityCreated& e) { state->entityEvents.push_back({0, e.entity.index, e.entity.generation, 0}); });
        w.events().subscribe<EntityDestroyed>([state](const EntityDestroyed& e) { state->entityEvents.push_back({1, e.entity.index, e.entity.generation, 0}); });
        w.events().subscribe<EntityReparented>([state](const EntityReparented& e) { state->entityEvents.push_back({2, e.entity.index, e.entity.generation, e.newParent.isNull() ? -1 : static_cast<int64_t>(e.newParent.index)}); });
        w.events().subscribe<EntityMoved>([state](const EntityMoved& e) { state->entityEvents.push_back({3, e.from.index, e.from.generation, static_cast<int64_t>(e.to.index)}); });
        return true;
    }
    if (name == "jobWorkers") {
        const int64_t n = std::clamp<int64_t>(a.integer("n", 1), 0, 16);
        st.jobs = std::make_unique<JobSystem>(static_cast<size_t>(n));
        w.setJobSystem(st.jobs.get());
        return true;
    }
    if (name == "rngDraw") {
        const std::string stream = a.str("stream", "bent");
        const int64_t n = std::clamp<int64_t>(a.integer("n", 1), 0, 100000);
        Pcg32& rng = w.random().stream(stream);
        for (int64_t k = 0; k < n; ++k) st.rngLog.emplace_back(stream, rng.next());
        return true;
    }
    if (name == "loadSceneText") {
        std::istringstream in(a.str("text", ""));
        st.loadOk = w.loadScene(in);
        return true;
    }
    if (name == "sceneMutate") {
        std::ostringstream out;
        w.saveScene(out);
        std::string text = out.str();
        const std::string ch = a.str("char", "x");
        const int64_t mode = a.integer("mode", 0);
        const size_t at = text.empty() ? 0 : static_cast<size_t>(std::max<int64_t>(0, a.integer("at", 0))) % text.size();
        if (mode == 0) text.insert(at, ch);
        else if (mode == 1) text.erase(at, 1);
        else if (mode == 2) text.replace(at, 1, ch);
        else if (mode == 3) text.resize(at);
        else if (mode == 5) {
            const size_t lineEnd = text.find('\n');
            if (lineEnd != std::string::npos) text = "LITSCENE " + std::to_string(a.integer("at", 3)) + text.substr(lineEnd);
        } else if (mode == 6) {
            text += "meta writer future-version\nfuturekeyword 1 2 3\tpayload\n";
        } else {
            const size_t start = text.rfind('\n', at) == std::string::npos ? 0 : text.rfind('\n', at) + 1;
            const size_t end = text.find('\n', start);
            const size_t next = end == std::string::npos ? std::string::npos : text.find('\n', end + 1);
            if (end != std::string::npos && next != std::string::npos) text = text.substr(0, start) + text.substr(end + 1, next - end) + text.substr(start, end - start + 1) + text.substr(next + 1);
        }
        std::istringstream in(text);
        st.loadOk = w.loadScene(in);
        return true;
    }
    if (name == "spawnBatch") {
        const int64_t n = std::clamp<int64_t>(a.integer("n", 1), 0, 20000);
        const glm::vec3 pos = a.vec("pos", glm::vec3(0.0f));
        const float spacing = static_cast<float>(a.num("spacing", 1.0));
        const int64_t mesh = a.integer("mesh", -1);
        std::set<uint64_t> known;
        for (const EntityHandle h : liveSorted(w)) known.insert((static_cast<uint64_t>(h.index) << 32) | h.generation);
        w.createBatch(static_cast<size_t>(n), [&](size_t index, EntityDesc& desc) {
            desc.name = "batch" + std::to_string(index);
            desc.mesh = mesh < 0 ? NO_MESH : static_cast<uint32_t>(mesh);
            desc.position = pos + glm::vec3(spacing * static_cast<float>(index), 0.0f, 0.0f);
        });
        for (const EntityHandle h : liveSorted(w)) {
            if (!known.contains((static_cast<uint64_t>(h.index) << 32) | h.generation)) (void)sim.refOf(h);
        }
        return true;
    }
    if (name == "teleportAll" || name == "teleportEach") {
        const glm::vec3 delta = a.vec("delta", glm::vec3(0.0f));
        std::vector<std::pair<EntityHandle, glm::vec3>> updates;
        for (const EntityHandle h : liveSorted(w)) updates.emplace_back(h, w.getPosition(h) + delta * static_cast<float>(1 + h.index % 3));
        if (name == "teleportAll") w.setPositions(updates);
        else {
            for (const auto& [h, p] : updates) w.setPosition(h, p);
        }
        return true;
    }
    if (name == "destroyBatch") {
        std::vector<EntityHandle> targets;
        if (const Value* list = a.get("targets")) {
            for (const Value& item : argList(*list, loc, "destroyBatch.targets")) targets.push_back(sim.resolve(item, loc));
        }
        if (a.integer("each", 0) != 0) {
            for (const EntityHandle h : targets) w.destroy(h);
        } else {
            w.destroyBatch(targets);
        }
        return true;
    }
    return false;
}

bool extWorldObserve(EngineSim& sim, const std::string& name, const ValueList& args, const SrcLoc& loc, Value& out) {
    const bool mine = std::any_of(kObservations.begin(), kObservations.end(), [&](const ObservationSpec& s) { return s.name == name; });
    if (!mine) return false;
    World& w = sim.world();
    ExtWorldState& st = stateOf(sim);
    const auto need = [&](size_t count) {
        if (args.size() != count) throw BentError(loc, name + "() takes " + std::to_string(count) + " argument(s), got " + std::to_string(args.size()));
    };
    const auto entityArg = [&](size_t index) { return sim.resolve(args[index], loc); };

    if (name == "worldMatrix") return need(1), out = matrixValue(w.getWorldMatrix(entityArg(0))), true;
    if (name == "worldMatrixRef") return need(1), out = matrixValue(chainMatrix(w, entityArg(0))), true;
    if (name == "worldRotation") return need(1), out = fromQuat(w.getWorldRotation(entityArg(0))), true;
    if (name == "forwardOf") return need(1), out = fromVec(w.forward(entityArg(0))), true;
    if (name == "layerOf") return need(1), out = Value::integer(w.getLayer(entityArg(0))), true;
    if (name == "inLayer") {
        need(1);
        std::vector<EntityHandle> found;
        w.forEachInLayer(static_cast<uint32_t>(argInt(args[0], loc, "inLayer")), [&](EntityHandle h) { found.push_back(h); });
        out = entityList(sim, std::move(found));
        return true;
    }
    if (name == "findByTag") return need(1), out = entityList(sim, w.findByTag(argString(args[0], loc, "findByTag"))), true;
    if (name == "findAllNamed") return need(1), out = entityList(sim, w.findAll(argString(args[0], loc, "findAllNamed"))), true;
    if (name == "findNamed") return need(1), out = sim.refOf(w.find(argString(args[0], loc, "findNamed"))), true;
    if (name == "componentNames") {
        ValueList list;
        for (const std::string& c : w.componentNames()) list.push_back(Value::string(c));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "componentText") {
        need(2);
        const std::optional<std::string> text = w.componentText(entityArg(0), argString(args[1], loc, "componentText"));
        out = text ? Value::string(*text) : Value::unit();
        return true;
    }
    if (name == "timerCount") return out = Value::integer(static_cast<int64_t>(w.timerCount())), true;
    if (name == "timerLog") {
        ValueList list;
        for (const auto& [id, tick] : st.timerLog) list.push_back(Value::makeList({Value::integer(id), Value::integer(tick)}));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "timerCancelResults") {
        ValueList list;
        for (const bool r : st.cancelResults) list.push_back(Value::boolean(r));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "busLog") {
        ValueList list;
        for (const auto& [id, value] : st.busLog) list.push_back(Value::makeList({Value::integer(id), Value::integer(value)}));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "busPending") return out = Value::integer(static_cast<int64_t>(w.events().pending())), true;
    if (name == "entityEvents") {
        ValueList list;
        for (const auto& e : st.entityEvents) list.push_back(Value::makeList({Value::integer(e[0]), Value::integer(e[1]), Value::integer(e[2]), Value::integer(e[3])}));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "rngLog") {
        ValueList list;
        for (const auto& [stream, value] : st.rngLog) list.push_back(Value::makeList({Value::string(stream), Value::integer(value)}));
        out = Value::makeList(std::move(list));
        return true;
    }
    if (name == "loadOk") return out = Value::boolean(st.loadOk), true;
    if (name == "subtreeHash") return need(1), out = Value::integer(asInt64(subtreeHash(w, entityArg(0), 0) & 0x7fffffffffffffffULL)), true;
    if (name == "snapshotString") return out = Value::string(w.snapshot()), true;
    if (name == "toLocalPoint") {
        need(1);
        const Value& v = args[0];
        if (v.kind != Kind::Vec3) throw BentError(loc, "toLocalPoint: expected a vec3");
        out = fromVec(w.toLocal(glm::dvec3(v.v[0], v.v[1], v.v[2])));
        return true;
    }
    if (name == "toWorldPoint") {
        need(1);
        out = fromDVec(w.toWorld(toVec(args[0], loc, "toWorldPoint")));
        return true;
    }
    if (name == "frustumQuery") return out = entityList(sim, w.queryFrustum(w.camera())), true;
    if (name == "frustumBrute") {
        const float slack = args.empty() ? 0.0f : static_cast<float>(argNumber(args[0], loc, "slack"));
        const glm::mat4 vp = w.camera().getProjectionMatrix() * w.camera().getViewMatrix();
        const glm::vec4 row0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        const glm::vec4 row1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        const glm::vec4 row2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        const glm::vec4 row3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
        std::array<glm::vec4, 6> planes = {row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2};
        for (glm::vec4& plane : planes) plane /= glm::length(glm::vec3(plane));
        std::vector<EntityHandle> found;
        for (const EntityHandle h : liveSorted(w)) {
            if (!w.isVisible(h)) continue;
            const glm::vec4 b = w.getWorldBounds(h);
            if (!(b.w > 0.0f)) continue;
            bool inside = true;
            for (const glm::vec4& plane : planes) {
                if (glm::dot(glm::vec3(plane), glm::vec3(b)) + plane.w < -b.w - slack) inside = false;
            }
            if (inside) found.push_back(h);
        }
        out = entityList(sim, std::move(found));
        return true;
    }
    if (name == "sceneBoundsOf") {
        const std::optional<glm::vec4> b = w.sceneBounds();
        out = Value::record({{"has", Value::boolean(b.has_value())}, {"center", fromVec(b ? glm::vec3(*b) : glm::vec3(0.0f))}, {"radius", Value::number(b ? b->w : 0.0f)}});
        return true;
    }
    if (name == "screenRayOf") {
        need(4);
        const Ray ray = w.screenRay(static_cast<float>(argNumber(args[0], loc, "x")), static_cast<float>(argNumber(args[1], loc, "y")), static_cast<float>(argNumber(args[2], loc, "w")), static_cast<float>(argNumber(args[3], loc, "h")));
        out = Value::record({{"origin", fromVec(ray.origin)}, {"direction", fromVec(ray.direction)}});
        return true;
    }
    if (name == "jobWorkerCount") return out = Value::integer(st.jobs ? static_cast<int64_t>(st.jobs->workerCount()) : 0), true;
    return false;
}

namespace {

ValueList u32List(const std::vector<uint32_t>& v) {
    ValueList out;
    out.reserve(v.size());
    for (const uint32_t x : v) out.push_back(Value::integer(x));
    return out;
}

void arity(const ValueList& a, size_t n, const SrcLoc& loc, const char* what) {
    if (a.size() != n) throw BentError(loc, std::string(what) + " takes " + std::to_string(n) + " arguments");
}

size_t count(const Value& v, const SrcLoc& loc, const char* what, int64_t limit) {
    const int64_t n = argInt(v, loc, what);
    if (n < 0 || n > limit) throw BentError(loc, std::string(what) + " must be in 0.." + std::to_string(limit));
    return static_cast<size_t>(n);
}

Pcg32 makeRng(const ValueList& a, const SrcLoc& loc) { return Pcg32(static_cast<uint64_t>(argInt(a[0], loc, "seed")), static_cast<uint64_t>(argInt(a[1], loc, "sequence"))); }

float asFloat(const Value& v, const SrcLoc& loc, const char* what) { return static_cast<float>(argNumber(v, loc, what)); }

}

void extWorldBuiltins(Program& program) {
    for (const ActionSpec& spec : kActions) {
        if (program.globals.contains(spec.name)) throw BentError(SrcLoc{"<host>", 0, 0}, "action '" + spec.name + "' shadows a builtin");
        program.native(
            spec.name,
            [spec](Interp&, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
                if (args.size() > spec.params.size()) throw BentError(loc, spec.name + "() takes at most " + std::to_string(spec.params.size()) + " arguments");
                std::vector<std::pair<std::string, Value>> fields;
                for (size_t index = 0; index < args.size(); ++index) fields.emplace_back(spec.params[index], args[index]);
                for (const auto& [key, value] : named) {
                    if (std::find(spec.params.begin(), spec.params.end(), key) == spec.params.end()) throw BentError(loc, spec.name + "() has no parameter '" + key + "'");
                    fields.emplace_back(key, value);
                }
                return Value::action(spec.name, std::move(fields));
            },
            true);
    }
    for (const ObservationSpec& spec : kObservations) {
        const std::string name = spec.name;
        if (program.globals.contains(name)) throw BentError(SrcLoc{"<host>", 0, 0}, "observation '" + name + "' shadows a builtin");
        program.native(name, [name](Interp& in, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
            if (!named.empty()) throw BentError(loc, "observation '" + name + "' takes positional arguments only");
            return in.currentSim(loc, name).observe(name, in, args, named, loc);
        });
    }

    const auto def = [&](const std::string& name, NativeFn fn) {
        if (program.globals.contains(name)) throw BentError(SrcLoc{"<host>", 0, 0}, "native '" + name + "' shadows a builtin");
        program.native(name, std::move(fn));
    };
    const auto pos = [](const ValueList& a, const NamedArgs&) { return a; };
    (void)pos;

    def("hashName", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "hashName(text)");
        return Value::integer(asInt64(hashName(argString(a[0], loc, "hashName"))));
    });
    def("rngSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "rngSeq(seed, sequence, n)");
        Pcg32 rng = makeRng(a, loc);
        const size_t n = count(a[2], loc, "n", 100000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::integer(rng.next()));
        return Value::makeList(std::move(out));
    });
    def("rngBelowSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "rngBelowSeq(seed, sequence, bound, n)");
        Pcg32 rng = makeRng(a, loc);
        const uint32_t bound = static_cast<uint32_t>(argInt(a[2], loc, "bound"));
        const size_t n = count(a[3], loc, "n", 200000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::integer(rng.below(bound)));
        return Value::makeList(std::move(out));
    });
    def("rngUniformSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "rngUniformSeq(seed, sequence, n)");
        Pcg32 rng = makeRng(a, loc);
        const size_t n = count(a[2], loc, "n", 200000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::number(rng.uniform()));
        return Value::makeList(std::move(out));
    });
    def("rngRangeSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 5, loc, "rngRangeSeq(seed, sequence, lo, hi, n)");
        Pcg32 rng = makeRng(a, loc);
        const float lo = asFloat(a[2], loc, "lo");
        const float hi = asFloat(a[3], loc, "hi");
        const size_t n = count(a[4], loc, "n", 100000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::number(rng.range(lo, hi)));
        return Value::makeList(std::move(out));
    });
    def("rngRangeIntSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 5, loc, "rngRangeIntSeq(seed, sequence, lo, hi, n)");
        Pcg32 rng = makeRng(a, loc);
        const int lo = static_cast<int>(argInt(a[2], loc, "lo"));
        const int hi = static_cast<int>(argInt(a[3], loc, "hi"));
        const size_t n = count(a[4], loc, "n", 100000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::integer(rng.rangeInt(lo, hi)));
        return Value::makeList(std::move(out));
    });
    def("rngChanceSeq", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "rngChanceSeq(seed, sequence, p, n)");
        Pcg32 rng = makeRng(a, loc);
        const float p = asFloat(a[2], loc, "p");
        const size_t n = count(a[3], loc, "n", 200000);
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::boolean(rng.chance(p)));
        return Value::makeList(std::move(out));
    });
    def("rngRestoreCheck", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "rngRestoreCheck(seed, sequence, skip, n)");
        Pcg32 rng = makeRng(a, loc);
        const size_t skip = count(a[2], loc, "skip", 100000);
        const size_t n = count(a[3], loc, "n", 10000);
        for (size_t k = 0; k < skip; ++k) rng.next();
        Pcg32 copy;
        copy.restore(rng.state(), rng.increment());
        ValueList original, restored;
        for (size_t k = 0; k < n; ++k) original.push_back(Value::integer(rng.next()));
        for (size_t k = 0; k < n; ++k) restored.push_back(Value::integer(copy.next()));
        return Value::record({{"original", Value::makeList(std::move(original))}, {"restored", Value::makeList(std::move(restored))}, {"equalObjects", Value::boolean(rng == copy)}});
    });
    def("rngService", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "rngService(seed, ops)");
        RandomService service;
        service.seed(static_cast<uint64_t>(argInt(a[0], loc, "seed")));
        ValueList results;
        for (const Value& op : argList(a[1], loc, "ops")) {
            const ValueList& pair = argList(op, loc, "op");
            if (pair.size() != 2) throw BentError(loc, "rngService op is [name, count]");
            const std::string stream = argString(pair[0], loc, "stream");
            if (stream == "@seed") {
                service.seed(static_cast<uint64_t>(argInt(pair[1], loc, "seed")));
                results.push_back(Value::makeList({}));
                continue;
            }
            const size_t n = count(pair[1], loc, "count", 10000);
            Pcg32& rng = service.stream(stream);
            ValueList draws;
            for (size_t k = 0; k < n; ++k) draws.push_back(Value::integer(rng.next()));
            results.push_back(Value::makeList(std::move(draws)));
        }
        return Value::makeList(std::move(results));
    });

    def("textTableRun", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "textTableRun(size, ops)");
        TextTable table;
        size_t size = count(a[0], loc, "size", 1000);
        for (size_t k = 0; k < size; ++k) table.grow();
        for (const Value& op : argList(a[1], loc, "ops")) {
            const ValueList& o = argList(op, loc, "op");
            const std::string kind = o.empty() ? "" : argString(o[0], loc, "op kind");
            const auto index = [&](size_t k) { return static_cast<Entity>(static_cast<uint64_t>(argInt(o.at(k), loc, "index")) % std::max<size_t>(size, 1)); };
            if (kind == "set" && size > 0) table.set(index(1), argString(o.at(2), loc, "text"));
            else if (kind == "erase" && size > 0) table.erase(index(1));
            else if (kind == "move" && size > 1) {
                const Entity from = index(1);
                const Entity to = index(2);
                if (from != to) table.move(from, to);
            } else if (kind == "resize") {
                size = count(o.at(1), loc, "size", 1000);
                if (size > table.size()) {
                    while (table.size() < size) table.grow();
                } else {
                    table.resize(size);
                }
            } else if (kind == "clear") {
                table.clear();
                size = 0;
            }
        }
        ValueList out;
        for (size_t k = 0; k < size; ++k) out.push_back(Value::string(table.get(static_cast<Entity>(k))));
        return Value::makeList(std::move(out));
    });

    def("jobsRun", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "jobsRun(count, grain, workers, submitters)");
        const size_t total = count(a[0], loc, "count", 200000);
        const size_t grain = count(a[1], loc, "grain", 100000);
        const size_t workers = count(a[2], loc, "workers", 16);
        const size_t submitters = std::max<size_t>(1, count(a[3], loc, "submitters", 8));
        JobSystem jobs(workers);
        std::vector<std::atomic<int>> visits(total);
        std::vector<uint64_t> values(total, 0);
        std::mutex mutex;
        std::vector<std::pair<size_t, size_t>> chunks;
        std::atomic<size_t> badChunks{0};
        const auto pass = [&](bool record) {
            jobs.parallelFor(total, grain, [&](size_t begin, size_t end) {
                if (begin >= end || end > total) badChunks.fetch_add(1);
                if (record) {
                    std::lock_guard lock(mutex);
                    chunks.emplace_back(begin, end);
                }
                for (size_t i = begin; i < end && i < total; ++i) {
                    visits[i].fetch_add(1);
                    values[i] = i * i + 7;
                }
            });
        };
        std::vector<std::thread> threads;
        for (size_t s = 1; s < submitters; ++s) threads.emplace_back([&] { pass(false); });
        pass(true);
        for (std::thread& t : threads) t.join();
        std::sort(chunks.begin(), chunks.end());
        ValueList visitList, valueList, chunkList;
        for (size_t i = 0; i < total; ++i) {
            visitList.push_back(Value::integer(visits[i].load()));
            valueList.push_back(Value::integer(static_cast<int64_t>(values[i])));
        }
        for (const auto& [b, e] : chunks) chunkList.push_back(Value::makeList({Value::integer(static_cast<int64_t>(b)), Value::integer(static_cast<int64_t>(e))}));
        return Value::record({{"visits", Value::makeList(std::move(visitList))}, {"values", Value::makeList(std::move(valueList))}, {"chunks", Value::makeList(std::move(chunkList))}, {"badChunks", Value::integer(static_cast<int64_t>(badChunks.load()))}});
    });

    def("asyncRun", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "asyncRun(workers, priorities, cancelMask)");
        const size_t workers = std::max<size_t>(1, count(a[0], loc, "workers", 8));
        const ValueList& prios = argList(a[1], loc, "priorities");
        const ValueList& mask = argList(a[2], loc, "cancelMask");
        const size_t n = prios.size();
        if (n > 2000) throw BentError(loc, "asyncRun: too many jobs");
        std::mutex mutex;
        std::vector<int64_t> order;
        std::vector<int> calls(n, 0);
        std::vector<int> sawCancelled(n, 0);
        std::atomic<bool> gateStarted{false};
        std::atomic<bool> gateOpen{false};
        uint64_t cancelledJobs = 0;
        size_t pendingAfter = 0;
        {
            AsyncJobs jobs(workers);
            jobs.submit(std::numeric_limits<int>::min(), [&] {
                gateStarted = true;
                while (!gateOpen) std::this_thread::yield();
            });
            while (!gateStarted) std::this_thread::yield();
            for (size_t k = 0; k < n; ++k) {
                auto token = std::make_shared<std::atomic<bool>>(k < mask.size() && mask[k].kind == Kind::Bool && mask[k].b);
                jobs.submit(static_cast<int>(argInt(prios[k], loc, "priority")), [&, k](bool cancelled) {
                    std::lock_guard lock(mutex);
                    order.push_back(static_cast<int64_t>(k));
                    ++calls[k];
                    if (cancelled) ++sawCancelled[k];
                }, token);
            }
            gateOpen = true;
            jobs.waitIdle();
            cancelledJobs = jobs.cancelledJobs();
            pendingAfter = jobs.pending();
        }
        ValueList orderList, callList, cancelList;
        for (const int64_t k : order) orderList.push_back(Value::integer(k));
        for (size_t k = 0; k < n; ++k) {
            callList.push_back(Value::integer(calls[k]));
            cancelList.push_back(Value::integer(sawCancelled[k]));
        }
        return Value::record({{"order", Value::makeList(std::move(orderList))}, {"calls", Value::makeList(std::move(callList))}, {"cancelled", Value::makeList(std::move(cancelList))}, {"cancelledJobs", Value::integer(static_cast<int64_t>(cancelledJobs))}, {"pending", Value::integer(static_cast<int64_t>(pendingAfter))}});
    });
    def("asyncDestroyRun", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "asyncDestroyRun(workers, n)");
        const size_t workers = std::max<size_t>(1, count(a[0], loc, "workers", 8));
        const size_t n = count(a[1], loc, "n", 2000);
        std::vector<std::atomic<int>> calls(n);
        {
            AsyncJobs jobs(workers);
            for (size_t k = 0; k < n; ++k) jobs.submit(static_cast<int>(k % 5), [&calls, k] { calls[k].fetch_add(1); });
        }
        ValueList out;
        for (size_t k = 0; k < n; ++k) out.push_back(Value::integer(calls[k].load()));
        return Value::makeList(std::move(out));
    });

    def("paramRun", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "paramRun(ops)");
        ParamBus bus;
        ValueList trace;
        for (const Value& op : argList(a[0], loc, "ops")) {
            const ValueList& o = argList(op, loc, "op");
            const std::string kind = o.empty() ? "" : argString(o[0], loc, "op kind");
            if (kind == "declare") bus.declare(argString(o.at(1), loc, "name"), asFloat(o.at(2), loc, "initial"), asFloat(o.at(3), loc, "smooth"), asFloat(o.at(4), loc, "min"), asFloat(o.at(5), loc, "max"));
            else if (kind == "set") bus.set(argString(o.at(1), loc, "name"), asFloat(o.at(2), loc, "value"));
            else if (kind == "snap") bus.snap(argString(o.at(1), loc, "name"), asFloat(o.at(2), loc, "value"));
            else if (kind == "update") bus.update(asFloat(o.at(1), loc, "dt"));
            else if (kind == "snapAll") bus.snapAll();
            ValueList row;
            for (const ParamInfo& info : bus.params()) row.push_back(Value::makeList({Value::number(info.value), Value::number(info.target)}));
            trace.push_back(Value::makeList(std::move(row)));
        }
        ValueList params;
        for (const ParamInfo& info : bus.params()) {
            params.push_back(Value::record({{"name", Value::string(info.name)}, {"slot", Value::integer(info.slot)}, {"value", Value::number(info.value)}, {"target", Value::number(info.target)}, {"min", Value::number(info.minimum)}, {"max", Value::number(info.maximum)}, {"smooth", Value::number(info.smooth)}, {"busValue", Value::number(bus.valueAt(info.slot))}}));
        }
        return Value::record({{"trace", Value::makeList(std::move(trace))}, {"params", Value::makeList(std::move(params))}});
    });
    def("curveEval", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "curveEval(points, mode, x)");
        ParamCurve curve;
        for (const Value& p : argList(a[0], loc, "points")) {
            const ValueList& xy = argList(p, loc, "point");
            if (xy.size() != 2) throw BentError(loc, "curve point is [x, y]");
            curve.points.emplace_back(asFloat(xy[0], loc, "x"), asFloat(xy[1], loc, "y"));
        }
        const int64_t mode = argInt(a[1], loc, "mode");
        curve.mode = mode == 1 ? CurveMode::Smooth : (mode == 2 ? CurveMode::Step : CurveMode::Linear);
        return Value::number(curve.eval(asFloat(a[2], loc, "x")));
    });
    def("paramLoad", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "paramLoad(text)");
        ParamBus bus;
        std::string error;
        const bool ok = bus.loadText(argString(a[0], loc, "text"), &error);
        ValueList slots;
        std::set<uint32_t> seen;
        bool distinct = true;
        for (const ParamInfo& info : bus.params()) {
            slots.push_back(Value::integer(info.slot));
            distinct = distinct && seen.insert(info.slot).second && info.slot < kParamSlots;
        }
        return Value::record({{"ok", Value::boolean(ok)}, {"error", Value::string(error)}, {"count", Value::integer(static_cast<int64_t>(bus.params().size()))}, {"slots", Value::makeList(std::move(slots))}, {"slotsDistinct", Value::boolean(distinct)}});
    });

    const auto buildReplay = [](const ValueList& a, const SrcLoc& loc) {
        ReplayData data;
        data.seed = static_cast<uint64_t>(argInt(a[0], loc, "seed"));
        for (const Value& t : argList(a[1], loc, "ticks")) {
            const ValueList& f = argList(t, loc, "tick");
            if (f.size() != 5) throw BentError(loc, "tick is [dt, keys, mouse, delta, buttons]");
            ReplayTick tick;
            tick.dt = asFloat(f[0], loc, "dt");
            for (const Value& key : argList(f[1], loc, "keys")) {
                const int64_t code = argInt(key, loc, "key");
                if (code >= 0 && code < kInputKeyCount) tick.input.keys.set(static_cast<size_t>(code));
            }
            const glm::vec3 mouse = toVec(f[2], loc, "mouse");
            const glm::vec3 delta = toVec(f[3], loc, "delta");
            tick.input.mouse = glm::vec2(mouse.x, mouse.y);
            tick.input.mouseDelta = glm::vec2(delta.x, delta.y);
            const int64_t buttons = argInt(f[4], loc, "buttons");
            for (size_t bit = 0; bit < tick.input.buttons.size(); ++bit) tick.input.buttons.set(bit, (buttons >> bit) & 1);
            data.ticks.push_back(tick);
        }
        return data;
    };
    def("replayText", [buildReplay](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "replayText(seed, ticks)");
        return Value::string(serializeReplay(buildReplay(a, loc)));
    });
    def("replayRoundtrip", [buildReplay](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "replayRoundtrip(seed, ticks)");
        const ReplayData data = buildReplay(a, loc);
        ReplayData back;
        std::string error;
        const bool ok = parseReplay(serializeReplay(data), back, &error);
        int64_t firstBad = -1;
        if (ok && back.ticks.size() == data.ticks.size()) {
            for (size_t k = 0; k < data.ticks.size(); ++k) {
                const bool same = back.ticks[k].dt == data.ticks[k].dt && back.ticks[k].input == data.ticks[k].input;
                if (!same) {
                    firstBad = static_cast<int64_t>(k);
                    break;
                }
            }
        }
        return Value::record({{"ok", Value::boolean(ok)}, {"error", Value::string(error)}, {"seedEqual", Value::boolean(ok && back.seed == data.seed)}, {"lengthEqual", Value::boolean(ok && back.ticks.size() == data.ticks.size())}, {"firstBad", Value::integer(firstBad)}});
    });
    def("utf8Encode", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "utf8Encode(codepoint)");
        const int64_t cp = argInt(a[0], loc, "codepoint");
        std::string out;
        if (cp < 0 || cp > 0x10FFFF) throw BentError(loc, "utf8Encode: code point out of range");
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) out += {static_cast<char>(0xC0 | (cp >> 6)), static_cast<char>(0x80 | (cp & 0x3F))};
        else if (cp < 0x10000) out += {static_cast<char>(0xE0 | (cp >> 12)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F))};
        else out += {static_cast<char>(0xF0 | (cp >> 18)), static_cast<char>(0x80 | ((cp >> 12) & 0x3F)), static_cast<char>(0x80 | ((cp >> 6) & 0x3F)), static_cast<char>(0x80 | (cp & 0x3F))};
        return Value::string(out);
    });
    def("strBytes", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "strBytes(text)");
        ValueList out;
        for (const unsigned char c : argString(a[0], loc, "text")) out.push_back(Value::integer(c));
        return Value::makeList(std::move(out));
    });
    def("bytesStr", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "bytesStr(bytes)");
        std::string out;
        for (const Value& b : argList(a[0], loc, "bytes")) out += static_cast<char>(argInt(b, loc, "byte") & 0xFF);
        return Value::string(out);
    });
    def("isValidUtf8", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "isValidUtf8(text)");
        const std::string& t = argString(a[0], loc, "text");
        size_t i = 0;
        while (i < t.size()) {
            const unsigned char c = static_cast<unsigned char>(t[i]);
            size_t len = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF ? 2 : (c >= 0xE0 && c <= 0xEF ? 3 : (c >= 0xF0 && c <= 0xF4 ? 4 : 0)));
            if (len == 0 || i + len > t.size()) return Value::boolean(false);
            uint32_t cp = len == 1 ? c : (len == 2 ? (c & 0x1F) : (len == 3 ? (c & 0x0F) : (c & 0x07)));
            for (size_t k = 1; k < len; ++k) {
                const unsigned char cc = static_cast<unsigned char>(t[i + k]);
                if ((cc & 0xC0) != 0x80) return Value::boolean(false);
                cp = (cp << 6) | (cc & 0x3F);
            }
            if ((len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return Value::boolean(false);
            i += len;
        }
        return Value::boolean(true);
    });
    def("jsonDiffOf", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "jsonDiffOf(a, b, epsilon)");
        return Value::string(jsonDiff(valueToJson(a[0], loc), valueToJson(a[1], loc), argNumber(a[2], loc, "epsilon")));
    });
    def("jsonFindAt", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "jsonFindAt(doc, path)");
        const Json doc = valueToJson(a[0], loc);
        const Json* found = jsonFind(doc, argString(a[1], loc, "path"));
        return Value::record({{"found", Value::boolean(found != nullptr)}, {"value", found ? jsonToValue(*found) : Value::unit()}});
    });
    def("gizmoSnap", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 2, loc, "gizmoSnap(value, step)");
        return Value::number(gizmo::snap(asFloat(a[0], loc, "value"), asFloat(a[1], loc, "step")));
    });
    def("gizmoRayPlane", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "gizmoRayPlane(origin, direction, planePoint, planeNormal)");
        Ray ray;
        ray.origin = toVec(a[0], loc, "origin");
        ray.direction = toVec(a[1], loc, "direction");
        const std::optional<glm::vec3> hit = gizmo::rayPlane(ray, toVec(a[2], loc, "planePoint"), toVec(a[3], loc, "planeNormal"));
        return Value::record({{"hit", Value::boolean(hit.has_value())}, {"point", fromVec(hit.value_or(glm::vec3(0.0f)))}});
    });
    def("gizmoSignedAngle", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "gizmoSignedAngle(from, to, axis)");
        return Value::number(gizmo::signedAngleAroundAxis(toVec(a[0], loc, "from"), toVec(a[1], loc, "to"), toVec(a[2], loc, "axis")));
    });
    def("gizmoClosestParam", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "gizmoClosestParam(rayOrigin, rayDirection, axisOrigin, axis)");
        Ray ray;
        ray.origin = toVec(a[0], loc, "origin");
        ray.direction = toVec(a[1], loc, "direction");
        return Value::number(gizmo::closestParameterOnAxis(ray, toVec(a[2], loc, "axisOrigin"), toVec(a[3], loc, "axis")));
    });
    def("gizmoEnclosingSphere", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "gizmoEnclosingSphere(spheres)");
        std::vector<glm::vec4> spheres;
        for (const Value& s : argList(a[0], loc, "spheres")) {
            const ValueList& f = argList(s, loc, "sphere");
            if (f.size() != 2) throw BentError(loc, "sphere is [center, radius]");
            spheres.emplace_back(toVec(f[0], loc, "center"), asFloat(f[1], loc, "radius"));
        }
        const glm::vec4 r = gizmo::enclosingSphere(spheres);
        return Value::record({{"center", fromVec(glm::vec3(r))}, {"radius", Value::number(r.w)}});
    });
    def("gizmoFrameDistance", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "gizmoFrameDistance(radius, fovYDegrees, aspect)");
        return Value::number(gizmo::frameDistance(asFloat(a[0], loc, "radius"), asFloat(a[1], loc, "fov"), asFloat(a[2], loc, "aspect")));
    });
    def("gizmoDistanceToSegment", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 3, loc, "gizmoDistanceToSegment(point, start, end)");
        const glm::vec3 p = toVec(a[0], loc, "point");
        const glm::vec3 s = toVec(a[1], loc, "start");
        const glm::vec3 e = toVec(a[2], loc, "end");
        return Value::number(gizmo::distanceToSegment2D(glm::vec2(p.x, p.y), glm::vec2(s.x, s.y), glm::vec2(e.x, e.y)));
    });
    def("gizmoRotateAbout", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 4, loc, "gizmoRotateAbout(matrix16, pivot, axis, degrees)");
        const ValueList& f = argList(a[0], loc, "matrix");
        if (f.size() != 16) throw BentError(loc, "matrix is 16 numbers (column major)");
        glm::mat4 m(1.0f);
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) m[c][r] = asFloat(f[static_cast<size_t>(c * 4 + r)], loc, "m");
        }
        return matrixValue(gizmo::rotateAboutWorldAxis(m, toVec(a[1], loc, "pivot"), toVec(a[2], loc, "axis"), glm::radians(asFloat(a[3], loc, "degrees"))));
    });
    def("replayParse", [](Interp&, const ValueList& a, const NamedArgs&, const SrcLoc& loc) {
        arity(a, 1, loc, "replayParse(text)");
        ReplayData data;
        std::string error;
        const bool ok = parseReplay(argString(a[0], loc, "text"), data, &error);
        return Value::record({{"ok", Value::boolean(ok)}, {"error", Value::string(error)}, {"ticks", Value::integer(static_cast<int64_t>(data.ticks.size()))}, {"seed", Value::integer(asInt64(data.seed))}});
    });
}

}
