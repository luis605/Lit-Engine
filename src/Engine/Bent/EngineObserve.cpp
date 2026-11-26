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
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>

#include <unistd.h>
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
#include "Engine/Bent/Ext_World.hpp"

namespace bent {

const std::vector<ObservationSpec>& engineObservationSpecs() {
    static const std::vector<ObservationSpec> specs = {
        {"entities", "entities()", "live entities (index order) as entity values"},
        {"spawned", "spawned()", "every entity the program created, including destroyed ones"},
        {"entityCount", "entityCount()", "World::aliveCount()"},
        {"liveCount", "liveCount()", "number of entities World::forEach visits"},
        {"alive", "alive(e)", "World::isAlive"},
        {"handle", "handle(e)", "{index, generation} of the entity's handle"},
        {"name", "name(e)", "entity name"},
        {"tag", "tag(e)", "entity tag"},
        {"parent", "parent(e)", "parent entity or -1"},
        {"children", "children(e)", "child entities"},
        {"position", "position(e)", "local position"},
        {"visible", "visible(e)", "World::isVisible (hidden entities are not in the spatial index)"},
        {"worldPosition", "worldPosition(e)", "absolute world position in double precision (origin + world matrix translation)"},
        {"rotation", "rotation(e)", "local rotation"},
        {"scale", "scale(e)", "local scale"},
        {"velocity", "velocity(e)", "rigid body velocity (vec3(0) without one)"},
        {"isDynamic", "isDynamic(e)", "has a non static RigidBody and no parent"},
        {"sleeping", "sleeping(e)", "rigid body is asleep"},
        {"bounds", "bounds(e)", "World::getWorldBounds as {center, radius}"},
        {"aabb", "aabb(e)", "world AABB of the entity's colliders {min, max} (bounding sphere without colliders)"},
        {"components", "components(e)", "names of the registered components the entity has"},
        {"hasNaN", "hasNaN()", "any non-finite local matrix, world position, velocity or camera value"},
        {"worldHash", "worldHash()", "hashWorldState (engine determinism hash)"},
        {"contentHash", "contentHash()", "order and handle independent hash of entity content (names, tags, mesh, local transforms quantised to 1e-4, components)"},
        {"relativeHash", "relativeHash()", "hash of everything except positions and the origin (rotations, scales, names, components...)"},
        {"energy", "energy()", "kinetic plus potential energy of awake and sleeping dynamic bodies (potential relative to y = 0 along gravity)"},
        {"kinetic", "kinetic()", "kinetic energy of dynamic bodies"},
        {"maxSpeed", "maxSpeed()", "largest dynamic body speed"},
        {"impulses", "impulses()", "number of impulse / setVelocity / force actions applied"},
        {"raycast", "raycast(from, to)", "World::raycast along the segment: {hit, entity, distance}"},
        {"physicsRaycast", "physicsRaycast(from, to)", "physicsRaycast along the segment: {hit, entity, distance, normal}"},
        {"overlap", "overlap(center, radius)", "World::overlapSphere, entities in index order"},
        {"cameraView", "cameraView()", "{pos (absolute), local, front, up, right, cell}"},
        {"cameraPos", "cameraPos()", "local camera position"},
        {"portalFrame", "portalFrame(i)", "portal i: {center, normal, up, right, half, partner}"},
        {"portalCount", "portalCount()", "number of portal ends"},
        {"portalViewError", "portalViewError()", "largest camera pose discontinuity across portal crossings in cameraWalk"},
        {"portalCrossings", "portalCrossings()", "camera crossings in cameraWalk"},
        {"portalPingPongs", "portalPingPongs()", "crossings straight back through the partner on the next step"},
        {"origin", "origin()", "floating origin (double)"},
        {"time", "time()", "world elapsed seconds"},
        {"frame", "frame()", "world frame counter"},
        {"ticks", "ticks()", "ticks run by this program"},
        {"resident", "resident(cell)", "grid cell index (0 based within the streaming grid) is resident"},
        {"residentCells", "residentCells()", "resident grid cells (0 based)"},
        {"cellCount", "cellCount()", "number of streaming grid cells"},
        {"cellBox", "cellBox(cell)", "local {min, max} of a grid cell (x/z extent, y 0..0)"},
        {"cellDistance", "cellDistance(cell)", "horizontal distance from the camera to the grid cell rectangle (local)"},
        {"loadRadius", "loadRadius()", "streaming load radius"},
        {"keepRadius", "keepRadius()", "streaming keep radius"},
        {"streamStats", "streamStats()", "{resident, pending, loaded, unloaded, residentBytes, settled}"},
        {"terrainStats", "terrainStats()", "{resident, wanted, loading, evicted, settled, maxResident}"},
        {"validatorIssues", "validatorIssues()", "validateScene issues as {severity, code}"},
        {"validatorWarnings", "validatorWarnings()", "count of warnings and errors from validateScene"},
        {"validatorErrors", "validatorErrors()", "count of errors from validateScene"},
        {"worldErrors", "worldErrors()", "World::validate() messages"},
        {"navPath", "navPath(from, to)", "{found, partial, points, length}"},
        {"navWalkable", "navWalkable(p)", "NavMesh::walkable"},
        {"navObstacles", "navObstacles()", "obstacle boxes {min, max}"},
        {"navRadius", "navRadius()", "agent radius of the nav mesh"},
        {"navCell", "navCell()", "nav cell size"},
        {"renderSetting", "renderSetting(key)", "renderSettingValue as a string"},
        {"renderSettingNames", "renderSettingNames()", "every render setting key"},
        {"deltaJson", "deltaJson()", "DeltaStore::toJson().dump()"},
        {"deltaCount", "deltaCount()", "DeltaStore::deltaCount()"},
        {"stateDump", "stateDump()", "Engine::dumpStateText(true) (debugging: compare two worlds with firstDiff)"},
        {"sceneText", "sceneText()", "World::saveScene output"},
        {"inputHeld", "inputHeld(key)", "simulated input key held this tick"},
    };
    return specs;
}

namespace {

double finiteOr(double value, double fallback) { return std::isfinite(value) ? value : fallback; }

bool finiteMat(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) return false;
        }
    }
    return true;
}

std::vector<EntityHandle> live(World& world) {
    std::vector<EntityHandle> out;
    world.forEach([&](EntityHandle h) { out.push_back(h); });
    std::sort(out.begin(), out.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
    return out;
}

Value boxValue(const glm::vec3& lo, const glm::vec3& hi) { return Value::record({{"min", fromVec(lo)}, {"max", fromVec(hi)}}); }

void hashEntityCommon(World& world, EntityHandle h, StateHasher& hasher) {
    hasher.addText(world.getName(h));
    hasher.addText(world.getTag(h));
    hasher.addU64(world.getLayer(h));
    hasher.addU64(world.getMesh(h));
    hasher.addBool(world.isVisible(h));
    const glm::quat q = world.getRotation(h);
    const float sign = q.w < 0.0f ? -1.0f : 1.0f;
    hasher.addFloat(q.w * sign);
    hasher.addVec3(q.x * sign, q.y * sign, q.z * sign);
    const glm::vec3 s = world.getScale(h);
    hasher.addVec3(s.x, s.y, s.z);
    for (const std::string& component : world.componentNames()) {
        if (const std::optional<std::string> text = world.componentText(h, component)) {
            hasher.addText(component);
            hasher.addText(*text);
        }
    }
}

}

uint64_t contentHash(World& world, bool absolute) {
    std::vector<uint64_t> parts;
    for (const EntityHandle h : live(world)) {
        StateHasher hasher;
        hashEntityCommon(world, h, hasher);
        const glm::vec3 p = absolute ? glm::vec3(world.toWorld(world.getWorldPosition(h))) : world.getPosition(h);
        hasher.addVec3(p.x, p.y, p.z);
        const EntityHandle parent = world.getParent(h);
        hasher.addText(parent.isNull() ? std::string() : world.getName(parent));
        parts.push_back(hasher.value());
    }
    std::sort(parts.begin(), parts.end());
    StateHasher total;
    total.addU64(parts.size());
    for (const uint64_t part : parts) total.addU64(part);
    return total.value();
}

uint64_t relativeHash(World& world) {
    StateHasher hasher;
    const std::vector<EntityHandle> handles = live(world);
    hasher.addU64(handles.size());
    for (const EntityHandle h : handles) {
        hasher.addU64(h.index);
        hashEntityCommon(world, h, hasher);
        const EntityHandle parent = world.getParent(h);
        hasher.addU64(parent.isNull() ? ~0ull : parent.index);
    }
    return hasher.value();
}

Value jsonToValue(const Json& json) {
    switch (json.type()) {
    case Json::Type::Null: return Value::unit();
    case Json::Type::Bool: return Value::boolean(json.asBool());
    case Json::Type::Number: return Value::number(json.asNumber());
    case Json::Type::String: return Value::string(json.asString());
    case Json::Type::Array: {
        ValueList items;
        for (const Json& item : json.items()) items.push_back(jsonToValue(item));
        return Value::makeList(std::move(items));
    }
    case Json::Type::Object: {
        std::vector<std::pair<std::string, Value>> fields;
        for (const auto& [key, item] : json.members()) fields.emplace_back(key, jsonToValue(item));
        return Value::record(std::move(fields));
    }
    }
    return Value::unit();
}

Json valueToJson(const Value& value, const SrcLoc& loc) {
    switch (value.kind) {
    case Kind::Unit: return Json();
    case Kind::Bool: return Json(value.b);
    case Kind::Int: return Json(value.i);
    case Kind::Float: return Json(value.f);
    case Kind::Str: return Json(value.text());
    case Kind::Vec3: return Json::array({Json(value.v[0]), Json(value.v[1]), Json(value.v[2])});
    case Kind::List: {
        Json out = Json::array();
        for (const Value& item : value.items()) out.push(valueToJson(item, loc));
        return out;
    }
    case Kind::Record: {
        Json out = Json::object();
        for (const auto& [key, item] : value.rec->fields) out.set(key, valueToJson(item, loc));
        return out;
    }
    default: throw BentError(loc, std::string("cannot convert a ") + kindName(value.kind) + " to JSON");
    }
}

Value EngineSim::observe(const std::string& name, Interp& interp, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
    (void)interp;
    if (!named.empty()) throw BentError(loc, "observation '" + name + "' takes positional arguments only");
    if (Value extResult; extWorldObserve(*this, name, args, loc, extResult)) return extResult;
    World& w = world();
    const auto need = [&](size_t count) {
        if (args.size() != count) throw BentError(loc, name + "() takes " + std::to_string(count) + " argument(s), got " + std::to_string(args.size()));
    };
    const auto entityArg = [&](size_t index) { return resolve(args[index], loc); };
    if (name == "entities") {
        need(0);
        ValueList out;
        for (const EntityHandle h : live(w)) out.push_back(refOf(h));
        return Value::makeList(std::move(out));
    }
    if (name == "spawned") {
        need(0);
        ValueList out;
        for (size_t ref = 0; ref < m_refs.size(); ++ref) out.push_back(Value::entity(static_cast<int64_t>(ref)));
        return Value::makeList(std::move(out));
    }
    if (name == "entityCount") return Value::integer(static_cast<int64_t>(w.aliveCount()));
    if (name == "liveCount") {
        int64_t count = 0;
        w.forEach([&](EntityHandle) { ++count; });
        return Value::integer(count);
    }
    if (name == "alive") {
        need(1);
        return Value::boolean(w.isAlive(entityArg(0)));
    }
    if (name == "handle") {
        need(1);
        const EntityHandle h = entityArg(0);
        return Value::record({{"index", Value::integer(h.index)}, {"generation", Value::integer(h.generation)}});
    }
    if (name == "name") {
        need(1);
        return Value::string(w.getName(entityArg(0)));
    }
    if (name == "tag") {
        need(1);
        return Value::string(w.getTag(entityArg(0)));
    }
    if (name == "parent") {
        need(1);
        return refOf(w.getParent(entityArg(0)));
    }
    if (name == "children") {
        need(1);
        ValueList out;
        for (const EntityHandle c : w.getChildren(entityArg(0))) out.push_back(refOf(c));
        return Value::makeList(std::move(out));
    }
    if (name == "visible") {
        need(1);
        return Value::boolean(w.isVisible(entityArg(0)));
    }
    if (name == "position") {
        need(1);
        return fromVec(w.getPosition(entityArg(0)));
    }
    if (name == "worldPosition") {
        need(1);
        return fromDVec(w.toWorld(w.getWorldPosition(entityArg(0))));
    }
    if (name == "rotation") {
        need(1);
        return fromQuat(w.getRotation(entityArg(0)));
    }
    if (name == "scale") {
        need(1);
        return fromVec(w.getScale(entityArg(0)));
    }
    if (name == "velocity") {
        need(1);
        const RigidBody* body = w.get<RigidBody>(entityArg(0));
        return fromVec(body ? body->velocity : glm::vec3(0.0f));
    }
    if (name == "isDynamic") {
        need(1);
        const EntityHandle h = entityArg(0);
        const RigidBody* body = w.get<RigidBody>(h);
        return Value::boolean(body && !body->isStatic && w.getParent(h).isNull());
    }
    if (name == "sleeping") {
        need(1);
        const RigidBody* body = w.get<RigidBody>(entityArg(0));
        return Value::boolean(body && body->sleeping);
    }
    if (name == "bounds") {
        need(1);
        const glm::vec4 b = w.getWorldBounds(entityArg(0));
        return Value::record({{"center", fromVec(glm::vec3(b))}, {"radius", Value::number(b.w)}});
    }
    if (name == "aabb") {
        need(1);
        const EntityHandle h = entityArg(0);
        const glm::mat4 m = w.getWorldMatrix(h);
        const glm::vec3 center(m[3]);
        const glm::mat3 basis(m);
        glm::vec3 extent(0.0f);
        bool any = false;
        if (const SphereCollider* s = w.get<SphereCollider>(h)) {
            extent = glm::max(extent, glm::vec3(s->radius));
            any = true;
        }
        if (const BoxCollider* b = w.get<BoxCollider>(h)) {
            const glm::mat3 r(glm::mat3_cast(w.getWorldRotation(h)));
            for (int axis = 0; axis < 3; ++axis) extent[axis] = std::max(extent[axis], std::abs(r[0][axis]) * b->halfExtents.x + std::abs(r[1][axis]) * b->halfExtents.y + std::abs(r[2][axis]) * b->halfExtents.z);
            any = true;
        }
        if (const CapsuleCollider* c = w.get<CapsuleCollider>(h)) {
            extent = glm::max(extent, glm::vec3(c->radius, c->radius + c->halfHeight, c->radius));
            any = true;
        }
        if (!any) {
            const glm::vec4 b = w.getWorldBounds(h);
            (void)basis;
            return boxValue(glm::vec3(b) - glm::vec3(b.w), glm::vec3(b) + glm::vec3(b.w));
        }
        return boxValue(center - extent, center + extent);
    }
    if (name == "components") {
        need(1);
        const EntityHandle h = entityArg(0);
        ValueList out;
        for (const std::string& component : w.componentNames()) {
            if (w.componentText(h, component)) out.push_back(Value::string(component));
        }
        return Value::makeList(std::move(out));
    }
    if (name == "hasNaN") {
        bool bad = false;
        w.forEach([&](EntityHandle h) {
            if (bad) return;
            if (!finiteMat(w.getLocalMatrix(h))) bad = true;
            const glm::vec3 p = w.getWorldPosition(h);
            if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) bad = true;
            if (const RigidBody* body = w.get<RigidBody>(h)) {
                if (!std::isfinite(body->velocity.x + body->velocity.y + body->velocity.z) || !std::isfinite(body->angularVelocity.x + body->angularVelocity.y + body->angularVelocity.z)) bad = true;
            }
        });
        const glm::vec3 c = w.camera().getPosition();
        if (!std::isfinite(c.x + c.y + c.z)) bad = true;
        return Value::boolean(bad);
    }
    if (name == "worldHash") return Value::integer(static_cast<int64_t>(hashWorldState(w) & 0x7fffffffffffffffULL));
    if (name == "contentHash") return Value::integer(static_cast<int64_t>(contentHash(w, false) & 0x7fffffffffffffffULL));
    if (name == "relativeHash") return Value::integer(static_cast<int64_t>(relativeHash(w) & 0x7fffffffffffffffULL));
    if (name == "energy" || name == "kinetic" || name == "maxSpeed") {
        const glm::vec3 g = m_engine->physics().gravity;
        double kinetic = 0.0;
        double potential = 0.0;
        double speed = 0.0;
        w.view<RigidBody>([&](EntityHandle h, RigidBody& body) {
            if (body.isStatic || !w.getParent(h).isNull()) return;
            const double v2 = glm::dot(body.velocity, body.velocity);
            kinetic += 0.5 * body.mass * v2;
            potential -= static_cast<double>(body.mass) * body.gravityScale * glm::dot(glm::dvec3(g), w.toWorld(w.getWorldPosition(h)));
            speed = std::max(speed, std::sqrt(v2));
        });
        if (name == "kinetic") return Value::number(kinetic);
        if (name == "maxSpeed") return Value::number(speed);
        return Value::number(kinetic + potential);
    }
    if (name == "impulses") return Value::integer(m_impulses);
    if (name == "raycast" || name == "physicsRaycast") {
        need(2);
        const glm::vec3 from = toVec(args[0], loc, "raycast from");
        const glm::vec3 to = toVec(args[1], loc, "raycast to");
        const glm::vec3 dir = to - from;
        const float length = glm::length(dir);
        if (name == "raycast") {
            const std::optional<RayHit> hit = w.raycast(from, dir, length);
            if (!hit) return Value::record({{"hit", Value::boolean(false)}, {"entity", Value::integer(-1)}, {"distance", Value::number(0.0)}});
            return Value::record({{"hit", Value::boolean(true)}, {"entity", refOf(hit->entity)}, {"distance", Value::number(hit->distance)}});
        }
        const std::optional<PhysicsHit> hit = length > 0.0f ? physicsRaycast(w, from, dir / length, length) : std::nullopt;
        if (!hit) return Value::record({{"hit", Value::boolean(false)}, {"entity", Value::integer(-1)}, {"distance", Value::number(0.0)}, {"normal", fromVec(glm::vec3(0.0f))}});
        return Value::record({{"hit", Value::boolean(true)}, {"entity", refOf(hit->entity)}, {"distance", Value::number(hit->distance)}, {"normal", fromVec(hit->normal)}});
    }
    if (name == "overlap") {
        need(2);
        ValueList out;
        for (const EntityHandle h : w.overlapSphere(toVec(args[0], loc, "overlap center"), static_cast<float>(argNumber(args[1], loc, "overlap radius")))) out.push_back(refOf(h));
        return Value::makeList(std::move(out));
    }
    if (name == "cameraView") {
        const Camera& camera = w.camera();
        return Value::record({{"pos", fromDVec(w.toWorld(camera.getPosition()))},
                              {"local", fromVec(camera.getPosition())},
                              {"front", fromVec(camera.getFront())},
                              {"up", fromVec(camera.getUp())},
                              {"right", fromVec(camera.getRight())},
                              {"cell", Value::integer(m_engine->portals().cameraCell())}});
    }
    if (name == "cameraPos") return fromVec(w.camera().getPosition());
    if (name == "portalFrame") {
        need(1);
        const std::vector<Portal>& portals = m_engine->portals().portals();
        const int64_t index = argInt(args[0], loc, "portalFrame");
        if (index < 0 || index >= static_cast<int64_t>(portals.size())) throw BentError(loc, "no portal " + std::to_string(index));
        const Portal& portal = portals[static_cast<size_t>(index)];
        return Value::record({{"center", fromVec(portal.center())}, {"normal", fromVec(portal.normal())}, {"up", fromVec(portal.up())}, {"right", fromVec(portal.right())},
                              {"half", fromVec(glm::vec3(portal.half, 0.0f))}, {"partner", Value::integer(portal.partner)}});
    }
    if (name == "portalCount") return Value::integer(static_cast<int64_t>(m_engine->portals().portals().size()));
    if (name == "portalViewError") return Value::number(m_portalError);
    if (name == "portalCrossings") return Value::integer(m_crossings);
    if (name == "portalPingPongs") return Value::integer(m_pingPongs);
    if (name == "origin") return fromDVec(w.origin());
    if (name == "time") return Value::number(w.time().elapsed);
    if (name == "frame") return Value::integer(static_cast<int64_t>(w.time().frame));
    if (name == "ticks") return Value::integer(static_cast<int64_t>(m_ticks));
    if (name == "cellCount") return Value::integer(m_grid.enabled ? m_grid.nx * m_grid.nz : 0);
    if (name == "loadRadius") return Value::number(m_grid.loadRadius);
    if (name == "keepRadius") return Value::number(m_grid.keepRadius);
    const auto gridCell = [&](size_t index) -> int {
        if (!m_grid.enabled) throw BentError(loc, name + "() needs streaming(...) in the program");
        const int64_t cell = argInt(args[index], loc, "cell");
        if (cell < 0 || cell >= m_grid.nx * m_grid.nz) throw BentError(loc, "cell " + std::to_string(cell) + " is outside the streaming grid");
        return static_cast<int>(cell);
    };
    if (name == "resident") {
        need(1);
        StreamingSystem* streaming = m_engine->streaming();
        return Value::boolean(streaming && streaming->resident(m_grid.first + gridCell(0)));
    }
    if (name == "residentCells") {
        ValueList out;
        if (StreamingSystem* streaming = m_engine->streaming()) {
            for (const int cell : streaming->residentCells()) {
                const int local = cell - m_grid.first;
                if (local >= 0 && local < m_grid.nx * m_grid.nz) out.push_back(Value::integer(local));
            }
        }
        std::sort(out.begin(), out.end(), [](const Value& a, const Value& b) { return a.i < b.i; });
        return Value::makeList(std::move(out));
    }
    if (name == "cellBox" || name == "cellDistance") {
        need(1);
        const int local = gridCell(0);
        const PortalCell& cell = m_engine->portals().cells()[static_cast<size_t>(m_grid.first + local)];
        if (name == "cellBox") return boxValue(glm::vec3(cell.min.x, 0.0f, cell.min.z), glm::vec3(cell.max.x, 0.0f, cell.max.z));
        const glm::vec3 c = w.camera().getPosition();
        const float dx = std::max({cell.min.x - c.x, 0.0f, c.x - cell.max.x});
        const float dz = std::max({cell.min.z - c.z, 0.0f, c.z - cell.max.z});
        return Value::number(std::sqrt(dx * dx + dz * dz));
    }
    if (name == "streamStats") {
        StreamingSystem* streaming = m_engine->streaming();
        if (!streaming) return Value::record({{"resident", Value::integer(0)}, {"pending", Value::integer(0)}, {"loaded", Value::integer(0)}, {"unloaded", Value::integer(0)}, {"residentBytes", Value::integer(0)}, {"settled", Value::boolean(true)}});
        const StreamingStats s = streaming->stats();
        return Value::record({{"resident", Value::integer(s.residentCells)},
                              {"pending", Value::integer(s.pendingCells)},
                              {"loaded", Value::integer(static_cast<int64_t>(s.cellsLoaded))},
                              {"unloaded", Value::integer(static_cast<int64_t>(s.cellsUnloaded))},
                              {"residentBytes", Value::integer(static_cast<int64_t>(s.residentBytes))},
                              {"settled", Value::boolean(streaming->settled())}});
    }
    if (name == "terrainStats") {
        TerrainStreamer* terrain = m_engine->terrain();
        if (!terrain) return Value::record({{"resident", Value::integer(0)}, {"wanted", Value::integer(0)}, {"loading", Value::integer(0)}, {"evicted", Value::integer(0)}, {"settled", Value::boolean(true)}, {"maxResident", Value::integer(0)}});
        const TerrainStats& s = terrain->stats();
        return Value::record({{"resident", Value::integer(s.resident)},
                              {"wanted", Value::integer(s.wanted)},
                              {"loading", Value::integer(s.loading)},
                              {"evicted", Value::integer(s.evicted)},
                              {"settled", Value::boolean(terrain->settled())},
                              {"maxResident", Value::integer(terrain->config().maxResidentTiles)}});
    }
    if (name == "validatorIssues" || name == "validatorWarnings" || name == "validatorErrors") {
        ValidatorLimits limits;
        limits.portals = &m_engine->portals();
        const std::vector<ValidationIssue> issues = validateScene(w, limits);
        if (name == "validatorWarnings") return Value::integer(static_cast<int64_t>(countIssues(issues, Severity::Warning) + countIssues(issues, Severity::Error)));
        if (name == "validatorErrors") return Value::integer(static_cast<int64_t>(countIssues(issues, Severity::Error)));
        ValueList out;
        for (const ValidationIssue& issue : issues) out.push_back(Value::record({{"severity", Value::string(severityName(issue.severity))}, {"code", Value::string(issue.code)}}));
        return Value::makeList(std::move(out));
    }
    if (name == "worldErrors") {
        ValueList out;
        for (const std::string& message : w.validate()) out.push_back(Value::string(message));
        return Value::makeList(std::move(out));
    }
    if (name == "navPath" || name == "navWalkable" || name == "navObstacles" || name == "navRadius" || name == "navCell") {
        if (name == "navObstacles") {
            ValueList out;
            for (const Aabb& box : m_nav.obstacles) out.push_back(boxValue(box.lo, box.hi));
            return Value::makeList(std::move(out));
        }
        if (name == "navRadius") return Value::number(m_nav.radius);
        if (name == "navCell") return Value::number(m_nav.cell);
        if (!m_nav.mesh) throw BentError(loc, name + "() needs navBuild(...) in the program");
        if (name == "navWalkable") {
            need(1);
            return Value::boolean(m_nav.mesh->walkable(toVec(args[0], loc, "navWalkable")));
        }
        need(2);
        const NavPath path = m_nav.mesh->findPath(toVec(args[0], loc, "navPath from"), toVec(args[1], loc, "navPath to"));
        ValueList points;
        for (const NavPathPoint& point : path.points) points.push_back(fromVec(point.position));
        return Value::record({{"found", Value::boolean(path.found)}, {"partial", Value::boolean(path.partial)}, {"points", Value::makeList(std::move(points))}, {"length", Value::number(path.length)}});
    }
    if (name == "renderSetting") {
        need(1);
        return Value::string(renderSettingValue(m_engine->renderSettings(), argString(args[0], loc, "renderSetting")));
    }
    if (name == "renderSettingNames") {
        ValueList out;
        visitRenderSettings(m_engine->renderSettings(), [&](std::string_view key, auto&) { out.push_back(Value::string(std::string(key))); });
        return Value::makeList(std::move(out));
    }
    if (name == "deltaJson") return Value::string(m_engine->deltas() ? m_engine->deltas()->toJson().dump() : std::string("{}"));
    if (name == "deltaCount") return Value::integer(m_engine->deltas() ? static_cast<int64_t>(m_engine->deltas()->deltaCount()) : 0);
    if (name == "stateDump") return Value::string(m_engine->dumpStateText(true));
    if (name == "sceneText") {
        std::ostringstream out;
        w.saveScene(out);
        return Value::string(out.str());
    }
    if (name == "inputHeld") {
        need(1);
        return Value::boolean(m_engine->simInput().held(static_cast<int>(argInt(args[0], loc, "inputHeld"))));
    }
    throw BentError(loc, "unknown observation '" + name + "'");
}

namespace {
std::atomic<uint64_t> gReplayCounter{0};
}

namespace xp {
void registerExtPortalBuiltins(Program& program);
}

namespace ext {
void registerExtRenderBuiltins(Program& program);
void registerExtGpuBuiltins(Program& program);
void registerExtPhysicsBuiltins(Program& program);
}

void EngineHost::addBuiltins(Program& program) {
    extWorldBuiltins(program);
    ext::registerExtRenderBuiltins(program);
    ext::registerExtGpuBuiltins(program);
    ext::registerExtPhysicsBuiltins(program);
    xp::registerExtPortalBuiltins(program);
    for (const ActionSpec& spec : engineActionSpecs()) {
        if (program.globals.contains(spec.name)) throw BentError(SrcLoc{"<host>", 0, 0}, "action '" + spec.name + "' shadows a builtin");
        program.native(
            spec.name,
            [spec](Interp&, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) {
                if (args.size() > spec.params.size()) throw BentError(loc, spec.name + "() takes at most " + std::to_string(spec.params.size()) + " arguments");
                std::vector<std::pair<std::string, Value>> fields;
                for (size_t index = 0; index < args.size(); ++index) fields.emplace_back(spec.params[index], args[index]);
                for (const auto& [key, value] : named) {
                    if (std::find(spec.params.begin(), spec.params.end(), key) == spec.params.end()) {
                        std::string valid;
                        for (const std::string& p : spec.params) valid += (valid.empty() ? "" : ", ") + p;
                        throw BentError(loc, spec.name + "() has no parameter '" + key + "' (parameters: " + (valid.empty() ? "none" : valid) + ")");
                    }
                    for (const auto& existing : fields) {
                        if (existing.first == key) throw BentError(loc, spec.name + "(): parameter '" + key + "' given twice");
                    }
                    fields.emplace_back(key, value);
                }
                return Value::action(spec.name, std::move(fields));
            },
            true);
    }
    for (const ObservationSpec& spec : engineObservationSpecs()) {
        const std::string name = spec.name;
        if (program.globals.contains(name)) throw BentError(SrcLoc{"<host>", 0, 0}, "observation '" + name + "' shadows a builtin");
        program.native(name, [name](Interp& in, const ValueList& args, const NamedArgs& named, const SrcLoc& loc) { return in.currentSim(loc, name).observe(name, in, args, named, loc); });
    }
    program.native("jsonDump", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "jsonDump(value)");
        return Value::string(valueToJson(args[0], loc).dump());
    });
    program.native("jsonPretty", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "jsonPretty(value)");
        return Value::string(valueToJson(args[0], loc).dump(2));
    });
    program.native("jsonParse", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "jsonParse(text)");
        std::string error;
        const std::optional<Json> parsed = Json::parse(argString(args[0], loc, "jsonParse"), &error);
        if (!parsed) return Value::record({{"ok", Value::boolean(false)}, {"error", Value::string(error)}, {"value", Value::unit()}});
        return Value::record({{"ok", Value::boolean(true)}, {"error", Value::string("")}, {"value", jsonToValue(*parsed)}});
    });
    program.native("replayMatches", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "replayMatches(program)");
        const std::vector<const ActionV*> actions = programActions(args[0], loc);
        const std::string path = (std::filesystem::temp_directory_path() / ("bent-replay-" + std::to_string(::getpid()) + "-" + std::to_string(gReplayCounter.fetch_add(1)) + ".replay")).string();
        uint64_t recorded = 0;
        uint64_t replayed = 0;
        {
            EngineSim sim;
            sim.startRecording(path);
            for (const ActionV* action : actions) sim.apply(*action, loc, [] { return true; });
            sim.stopRecording();
            recorded = sim.replayHash();
        }
        {
            EngineSim sim;
            if (!sim.startReplay(path)) {
                std::filesystem::remove(path);
                throw BentError(loc, "replay file could not be loaded back");
            }
            for (const ActionV* action : actions) {
                if (action->name == "input") continue;
                sim.apply(*action, loc, [] { return true; });
            }
            replayed = sim.replayHash();
        }
        std::error_code ec;
        std::filesystem::remove(path, ec);
        return Value::boolean(recorded == replayed);
    });
}

}
