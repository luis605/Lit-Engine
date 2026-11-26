#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

import Engine.World;
import Engine.Physics;
import Engine.Portal;
import Engine.PortalPhysics;
import Engine.Character;
import Engine.WorldHash;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.camera;
import Engine.CameraRig;
import Engine.glm;

#include "Engine/Bent/Ext_PhysicsUtil.hpp"

namespace bent::ext {

void registerExtSimBuiltins(Program& program);

namespace {

constexpr float kStep = 1.0f / 60.0f;

struct PxWorld {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<EntityHandle> ents;
    PhysicsSettings settings;
    PortalSystem portals;
    bool hasPortals = false;
};

void addPortals(PortalSystem& portals, const ValueList& list, const SrcLoc& loc) {
    if (list.empty()) return;
    PortalCell cell;
    cell.name = "main";
    portals.addCell(cell);
    int n = 0;
    for (const Value& pv : list) {
        const Rec pr = rec(pv, loc);
        PortalEnd a;
        a.cell = "main";
        a.position = pr.vec("a", glm::vec3(0.0f, 1.5f, -5.0f));
        a.eulerDegrees = pr.vec("aEuler", glm::vec3(0.0f));
        const glm::vec3 half = pr.vec("half", glm::vec3(1.5f, 1.5f, 0.0f));
        a.halfSize = glm::vec2(half.x, half.y);
        PortalEnd b = a;
        b.position = pr.vec("b", glm::vec3(20.0f, 1.5f, -5.0f));
        b.eulerDegrees = pr.vec("bEuler", glm::vec3(0.0f, 180.0f, 0.0f));
        portals.addPair("pair" + std::to_string(n++), a, b, pr.f("twist", 0.0f));
    }
    installPortalPhysics(portals);
}

std::vector<EntityHandle> liveSorted(World& world) {
    std::vector<EntityHandle> out;
    world.forEach([&](EntityHandle h) { out.push_back(h); });
    std::sort(out.begin(), out.end(), [](const EntityHandle& a, const EntityHandle& b) { return a.index < b.index; });
    return out;
}

EntityHandle addBody(PxWorld& px, const Rec& r) {
    World& w = *px.world;
    const std::string shape = r.str("shape", "sphere");
    EntityDesc desc;
    desc.name = "b" + std::to_string(px.ents.size());
    desc.mesh = NO_MESH;
    desc.position = r.vec("pos", glm::vec3(0.0f));
    desc.rotation = r.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    desc.scale = r.vec("scale", glm::vec3(1.0f));
    const EntityHandle h = w.create(desc);
    px.ents.push_back(h);
    if (h.isNull()) return h;
    if (r.has("layer")) w.setLayer(h, static_cast<uint32_t>(r.integer("layer", 1)));
    const Value* sizeValue = r.get("size");
    const glm::vec3 half = sizeValue && sizeValue->kind == Kind::Vec3 ? vecOf(*sizeValue, r.loc, "size") : glm::vec3(r.f("size", 0.5f));
    const float radius = sizeValue && sizeValue->kind == Kind::Vec3 ? half.x : r.f("size", 0.5f);
    bool surface = false;
    if (shape == "sphere") w.add<SphereCollider>(h, SphereCollider{radius});
    else if (shape == "box") w.add<BoxCollider>(h, BoxCollider{half});
    else if (shape == "capsule") w.add<CapsuleCollider>(h, CapsuleCollider{radius, r.f("height", 0.5f)});
    else if (shape == "hull") {
        std::vector<glm::vec3> points;
        for (const Value& p : r.list("points")) points.push_back(vecOf(p, r.loc, "points"));
        if (points.empty()) {
            for (int corner = 0; corner < 8; ++corner) points.emplace_back((corner & 1) ? half.x : -half.x, (corner & 2) ? half.y : -half.y, (corner & 4) ? half.z : -half.z);
        }
        if (auto hull = makeConvexHull(points)) w.add<ConvexHullCollider>(h, ConvexHullCollider{hull});
    } else if (shape == "compound") {
        CompoundCollider compound;
        for (const Value& pv : r.list("parts")) {
            const Rec pr = rec(pv, r.loc);
            CompoundPart part;
            const std::string kind = pr.str("kind", "sphere");
            part.kind = kind == "box" ? CompoundPart::Kind::Box : kind == "capsule" ? CompoundPart::Kind::Capsule : CompoundPart::Kind::Sphere;
            part.offset = pr.vec("offset", glm::vec3(0.0f));
            part.radius = pr.f("radius", 0.5f);
            part.halfExtents = pr.vec("half", glm::vec3(0.5f));
            part.halfHeight = pr.f("height", 0.5f);
            compound.parts.push_back(part);
        }
        w.add<CompoundCollider>(h, std::move(compound));
    } else if (shape == "plane") {
        PlaneCollider plane;
        plane.normal = r.vec("normal", glm::vec3(0.0f, 1.0f, 0.0f));
        plane.restitution = r.f("restitution", 0.0f);
        w.add<PlaneCollider>(h, plane);
        surface = true;
    } else if (shape == "mesh") {
        std::vector<glm::vec3> verts;
        for (const Value& p : r.list("verts")) verts.push_back(vecOf(p, r.loc, "verts"));
        std::vector<uint32_t> idx;
        for (const Value& i : r.list("idx")) idx.push_back(static_cast<uint32_t>(argInt(i, r.loc, "idx")));
        TriMeshCollider collider;
        collider.data = makeTriangleMesh(std::move(verts), std::move(idx));
        collider.restitution = r.f("restitution", 0.0f);
        w.add<TriMeshCollider>(h, collider);
        surface = true;
    } else if (shape == "heightfield") {
        auto data = std::make_shared<HeightfieldData>();
        data->samplesX = static_cast<int>(r.integer("nx", 2));
        data->samplesZ = static_cast<int>(r.integer("nz", 2));
        data->cellSize = r.f("cell", 1.0f);
        for (const Value& hv : r.list("heights")) data->heights.push_back(static_cast<float>(argNumber(hv, r.loc, "heights")));
        data->updateBounds();
        HeightfieldCollider collider;
        collider.data = data;
        collider.restitution = r.f("restitution", 0.0f);
        w.add<HeightfieldCollider>(h, collider);
        surface = true;
    } else if (shape != "none") {
        throw BentError(r.loc, "pxRun: unknown shape '" + shape + "'");
    }
    if (!surface && shape != "none") {
        RigidBody body;
        body.isStatic = !r.flag("dynamic", true);
        body.mass = r.f("mass", 1.0f);
        body.restitution = r.f("restitution", 0.0f);
        body.friction = r.f("friction", 0.5f);
        body.staticFriction = r.f("staticFriction", std::max(0.6f, body.friction));
        body.velocity = r.vec("vel", glm::vec3(0.0f));
        body.angularVelocity = r.vec("ang", glm::vec3(0.0f));
        body.gravityScale = r.f("gravityScale", 1.0f);
        body.rollingResistance = r.f("rolling", 0.0f);
        body.sleeping = r.flag("sleeping", false);
        w.add<RigidBody>(h, body);
    }
    return h;
}

void buildScene(PxWorld& px, const Value& scene, const SrcLoc& loc) {
    registerPhysicsComponents(*px.world);
    const Rec s = rec(scene, loc);
    px.settings.gravity = s.vec("gravity", glm::vec3(0.0f, -9.81f, 0.0f));
    px.settings.solverIterations = static_cast<int>(s.integer("iterations", 2));
    px.settings.sleepVelocity = s.f("sleepVelocity", 0.1f);
    px.settings.sleepTime = s.f("sleepTime", 0.5f);
    for (const Value& m : s.list("matrix")) {
        const ValueList& t = m.items();
        if (t.size() == 3) px.settings.setLayersCollide(static_cast<uint32_t>(argInt(t[0], loc, "matrix")), static_cast<uint32_t>(argInt(t[1], loc, "matrix")), t[2].kind == Kind::Bool && t[2].b);
    }
    if (!s.list("portals").empty()) {
        addPortals(px.portals, s.list("portals"), loc);
        px.hasPortals = true;
    }
    for (const Value& b : s.list("bodies")) {
        const Rec br = rec(b, loc);
        const EntityHandle h = addBody(px, br);
        if (px.hasPortals && br.flag("track", false) && !h.isNull()) px.portals.track(*px.world, h);
    }
    if (s.flag("roundtrip", false)) {
        std::stringstream text;
        if (!px.world->saveScene(text)) throw BentError(loc, "pxRun: saveScene failed");
        auto fresh = std::make_unique<World>();
        registerPhysicsComponents(*fresh);
        std::istringstream in(text.str());
        if (!fresh->loadScene(in)) throw BentError(loc, "pxRun: loadScene rejected the scene it just saved");
        px.ents = liveSorted(*fresh);
        px.world = std::move(fresh);
    }
}

Value bodyState(PxWorld& px, size_t index) {
    World& w = *px.world;
    const EntityHandle h = px.ents[index];
    if (!w.isAlive(h)) return recV({{"alive", boolV(false)}});
    const RigidBody* body = w.get<RigidBody>(h);
    Value ghost = Value::unit();
    if (px.hasPortals) {
        const EntityHandle g = px.portals.ghostOf(h);
        if (w.isAlive(g)) {
            const RigidBody* gb = w.get<RigidBody>(g);
            ghost = recV({{"pos", vecV(w.getWorldPosition(g))}, {"visible", boolV(w.isVisible(g))}, {"dynamic", boolV(gb && !gb->isStatic)}});
        }
    }
    return recV({{"ghost", ghost},
                 {"cell", intV(px.hasPortals ? px.portals.objectCell(h) : -1)},
                 {"alive", boolV(true)},
                 {"pos", vecV(w.getWorldPosition(h))},
                 {"rot", quatV(w.getRotation(h))},
                 {"vel", vecV(body ? body->velocity : glm::vec3(0.0f))},
                 {"ang", vecV(body ? body->angularVelocity : glm::vec3(0.0f))},
                 {"sleeping", boolV(body && body->sleeping)},
                 {"mass", numV(body ? body->mass : 0.0)},
                 {"dynamic", boolV(body && !body->isStatic)}});
}

bool anyNaN(PxWorld& px) {
    World& w = *px.world;
    for (const EntityHandle h : px.ents) {
        if (!w.isAlive(h)) continue;
        if (!finite3(w.getWorldPosition(h))) return true;
        if (const RigidBody* b = w.get<RigidBody>(h)) {
            if (!finite3(b->velocity) || !finite3(b->angularVelocity)) return true;
        }
    }
    return false;
}

Value allStates(PxWorld& px) {
    ValueList out;
    for (size_t i = 0; i < px.ents.size(); ++i) out.push_back(bodyState(px, i));
    return listV(std::move(out));
}

Value pxRun(const Value& scene, const SrcLoc& loc) {
    PxWorld px;
    buildScene(px, scene, loc);
    const Rec s = rec(scene, loc);
    World& w = *px.world;
    std::vector<Value> collisions;
    std::map<std::pair<uint32_t, uint32_t>, int64_t> indexOf;
    for (size_t i = 0; i < px.ents.size(); ++i) indexOf[{px.ents[i].index, px.ents[i].generation}] = static_cast<int64_t>(i);
    int64_t tick = 0;
    const auto idx = [&](EntityHandle h) {
        const auto it = indexOf.find({h.index, h.generation});
        return it == indexOf.end() ? int64_t(-1) : it->second;
    };
    const uint32_t sub = w.events().subscribe<Collision>([&](const Collision& c) {
        if (collisions.size() < 4000) collisions.push_back(recV({{"tick", intV(tick)}, {"a", intV(idx(c.a))}, {"b", intV(idx(c.b))}, {"normal", vecV(c.normal)}, {"depth", numV(c.depth)}, {"closing", numV(c.closingSpeed)}}));
    });
    const int64_t steps = s.integer("steps", 0);
    const int64_t every = s.integer("record", 0);
    ValueList frames;
    if (every > 0) frames.push_back(allStates(px));
    const bool sizes = s.has("kicks");
    for (tick = 0; tick < steps; ++tick) {
        if (sizes) {
            for (const Value& kv : s.list("kicks")) {
                const Rec k = rec(kv, loc);
                if (k.integer("tick", 0) != tick) continue;
                const size_t target = static_cast<size_t>(k.integer("target", 0)) % px.ents.size();
                if (RigidBody* body = w.get<RigidBody>(px.ents[target]); body && body->mass > 0.0f) {
                    body->velocity += k.vec("j", glm::vec3(0.0f)) / body->mass;
                    wakeBody(w, px.ents[target]);
                }
            }
        }
        if (px.hasPortals) stepPortalPhysics(px.portals, w, kStep, px.settings);
        else stepPhysics(w, kStep, px.settings);
        w.events().dispatch();
        if (every > 0 && (tick + 1) % every == 0) frames.push_back(allStates(px));
    }
    w.events().unsubscribe(sub);
    return recV({{"bodies", allStates(px)},
                 {"frames", listV(std::move(frames))},
                 {"collisions", listV(std::move(collisions))},
                 {"hasNaN", boolV(anyNaN(px))},
                 {"entityCount", intV(static_cast<int64_t>(liveSorted(w).size()))},
                 {"hash", intV(static_cast<int64_t>(hashWorldState(w) & 0x7fffffffffffffffULL))}});
}

Value hitValue(const std::optional<PhysicsHit>& hit, const std::map<std::pair<uint32_t, uint32_t>, int64_t>& indexOf) {
    if (!hit) return recV({{"hit", boolV(false)}, {"entity", intV(-1)}, {"distance", numV(0)}, {"normal", vecV(glm::vec3(0.0f))}});
    const auto it = indexOf.find({hit->entity.index, hit->entity.generation});
    return recV({{"hit", boolV(true)}, {"entity", intV(it == indexOf.end() ? -1 : it->second)}, {"distance", numV(hit->distance)}, {"normal", vecV(hit->normal)}});
}

Value pxCast(const Value& scene, const Value& query, const SrcLoc& loc) {
    PxWorld px;
    buildScene(px, scene, loc);
    World& w = *px.world;
    std::map<std::pair<uint32_t, uint32_t>, int64_t> indexOf;
    for (size_t i = 0; i < px.ents.size(); ++i) indexOf[{px.ents[i].index, px.ents[i].generation}] = static_cast<int64_t>(i);
    const Rec q = rec(query, loc);
    std::vector<int64_t> skipList;
    for (const Value& v : q.list("skip")) skipList.push_back(argInt(v, loc, "skip"));
    const PhysicsFilter skip = skipList.empty() ? PhysicsFilter{} : PhysicsFilter([&](EntityHandle h) {
        const auto it = indexOf.find({h.index, h.generation});
        return it != indexOf.end() && std::find(skipList.begin(), skipList.end(), it->second) != skipList.end();
    });
    const std::string kind = q.str("kind", "ray");
    const glm::vec3 from = q.vec("from", glm::vec3(0.0f));
    const glm::vec3 dir = q.vec("dir", glm::vec3(0.0f, 0.0f, -1.0f));
    const float dist = q.f("dist", 100.0f);
    const float radius = q.f("radius", 0.5f);
    const auto contacts = [&](const std::vector<PhysicsContact>& list) {
        ValueList out;
        for (const PhysicsContact& c : list) {
            const auto it = indexOf.find({c.entity.index, c.entity.generation});
            out.push_back(recV({{"entity", intV(it == indexOf.end() ? -1 : it->second)}, {"normal", vecV(c.normal)}, {"depth", numV(c.depth)}}));
        }
        return recV({{"contacts", listV(std::move(out))}});
    };
    if (kind == "portalRay" || kind == "portalSweep") {
        const PortalCast cast = kind == "portalRay" ? portalRaycast(px.portals, w, from, dir, dist, -1, skip) : portalSweep(px.portals, w, from, dir, dist, radius, -1, skip);
        Value hit = hitValue(cast.hit, indexOf);
        return recV({{"hit", *hit.field("hit")}, {"entity", *hit.field("entity")}, {"distance", *hit.field("distance")}, {"normal", *hit.field("normal")}, {"crossings", intV(cast.crossings)}, {"point", vecV(cast.point)}, {"direction", vecV(cast.direction)}, {"total", numV(cast.distance)}});
    }
    if (kind == "ray") return hitValue(physicsRaycast(w, from, dir, dist, skip), indexOf);
    if (kind == "sweepSphere") return hitValue(physicsSweepSphere(w, from, dir, dist, radius, skip), indexOf);
    if (kind == "sweepCapsule") return hitValue(physicsSweepCapsule(w, q.vec("a", from), q.vec("b", from), radius, dir, dist, skip), indexOf);
    if (kind == "sweepBox") return hitValue(physicsSweepBox(w, from, q.vec("half", glm::vec3(0.5f)), dir, dist, skip), indexOf);
    if (kind == "overlapBox") return contacts(physicsOverlapBox(w, from, q.vec("half", glm::vec3(0.5f)), skip));
    if (kind == "overlapCapsule") return contacts(physicsOverlapCapsule(w, q.vec("a", from), q.vec("b", from), radius, skip));
    throw BentError(loc, "pxCast: unknown query kind '" + kind + "'");
}

Value pxChar(const Value& scene, const Value& specValue, const SrcLoc& loc) {
    PxWorld px;
    buildScene(px, scene, loc);
    World& w = *px.world;
    const Rec spec = rec(specValue, loc);
    PortalSystem& portals = px.portals;
    addPortals(portals, spec.list("portals"), loc);
    CharacterSettings cs;
    const Value settingsValue = spec.has("settings") ? *spec.get("settings") : Value::unit();
    const Rec set = rec(settingsValue, loc);
    cs.radius = set.f("radius", cs.radius);
    cs.height = set.f("height", cs.height);
    cs.stepHeight = set.f("stepHeight", cs.stepHeight);
    cs.slopeLimit = set.f("slopeLimit", cs.slopeLimit);
    cs.speed = set.f("speed", cs.speed);
    cs.jumpSpeed = set.f("jumpSpeed", cs.jumpSpeed);
    cs.acceleration = set.f("acceleration", cs.acceleration);
    CharacterController ctrl(w, portals, px.settings, cs);
    ctrl.spawn(spec.vec("feet", glm::vec3(0.0f)), spec.f("yaw", 0.0f));
    const bool stepWorld = spec.flag("stepWorld", false);
    ValueList samples;
    int64_t tick = 0;
    for (const Value& item : spec.list("script")) {
        const Rec it = rec(item, loc);
        CharacterInput input;
        input.move = glm::vec2(it.f("mx", 0.0f), it.f("mz", 0.0f));
        input.jump = it.flag("jump", false);
        input.sprint = it.flag("sprint", false);
        const int64_t ticks = it.integer("ticks", 1);
        for (int64_t i = 0; i < ticks; ++i, ++tick) {
            ctrl.setInput(input);
            if (i > 0) {
                CharacterInput held = input;
                held.jump = false;
                ctrl.setInput(held);
            }
            if (stepWorld) stepPortalPhysics(portals, w, kStep, px.settings);
            ctrl.update(kStep);
            const glm::vec3 p = ctrl.position();
            const glm::vec3 half(cs.radius - 0.02f, cs.height * 0.5f - 0.02f, cs.radius - 0.02f);
            const EntityHandle self = ctrl.entity();
            const PhysicsFilter skip = [&](EntityHandle e) {
                if (e == self || w.getParent(e) == self) return true;
                const RigidBody* body = w.get<RigidBody>(e);
                return body && !body->isStatic;
            };
            float depth = 0.0f;
            for (const PhysicsContact& c : physicsOverlapBox(w, p + glm::vec3(0.0f, cs.height * 0.5f, 0.0f), half, skip)) depth = std::max(depth, c.depth);
            samples.push_back(recV({{"pos", vecV(p)},
                                    {"vel", vecV(ctrl.velocity())},
                                    {"grounded", boolV(ctrl.grounded())},
                                    {"up", vecV(ctrl.up())},
                                    {"normal", vecV(ctrl.groundNormal())},
                                    {"depth", numV(depth)},
                                    {"crossings", intV(ctrl.crossings())},
                                    {"cell", intV(ctrl.cell())}}));
        }
    }
    return recV({{"samples", listV(std::move(samples))}});
}

}

void registerExtPhysicsBuiltins(Program& program) {
    program.native("pxRun", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "pxRun(scene)");
        return pxRun(args[0], loc);
    });
    program.native("pxCast", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "pxCast(scene, query)");
        return pxCast(args[0], args[1], loc);
    });
    program.native("pxChar", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "pxChar(scene, spec)");
        return pxChar(args[0], args[1], loc);
    });
    program.native("pxPortalFrames", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "pxPortalFrames(portals)");
        PortalSystem portals;
        addPortals(portals, args[0].items(), loc);
        ValueList out;
        for (const Portal& p : portals.portals()) {
            out.push_back(recV({{"center", vecV(p.center())}, {"normal", vecV(p.normal())}, {"up", vecV(p.up())}, {"right", vecV(p.right())}, {"half", vecV(glm::vec3(p.half, 0.0f))}, {"partner", intV(p.partner)}}));
        }
        return listV(std::move(out));
    });
    registerExtSimBuiltins(program);
}

}
