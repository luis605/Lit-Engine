#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Engine/Bent/Interp.hpp"

import Engine.World;
import Engine.Portal;
import Engine.Portal.view;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.camera;
import Engine.glm;

#include "Engine/Bent/Ext_PortalUtil.hpp"

namespace bent::xp {

void registerExtPortalStreaming(Program& program);
void registerExtPortalTerrain(Program& program);
void registerExtPortalSave(Program& program);

namespace {

constexpr float kPi = 3.14159265358979f;

struct PairSpec {
    glm::vec3 a{0.0f, 1.5f, -5.0f};
    glm::vec3 ae{0.0f};
    glm::vec3 b{20.0f, 1.5f, -5.0f};
    glm::vec3 be{0.0f, 180.0f, 0.0f};
    glm::vec2 half{1.5f, 1.5f};
    glm::vec2 halfB{1.5f, 1.5f};
    float twist = 0.0f;
    int cellA = 0;
    int cellB = 1;
    PortalLook look;
};

PairSpec readPair(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    PairSpec spec;
    spec.a = r.vec("a", spec.a);
    spec.ae = r.vec("ae", spec.ae);
    spec.b = r.vec("b", spec.b);
    spec.be = r.vec("be", spec.be);
    const glm::vec3 half = r.vec("half", glm::vec3(1.5f, 1.5f, 0.0f));
    spec.half = glm::vec2(half.x, half.y);
    const glm::vec3 halfB = r.vec("halfB", half);
    spec.halfB = glm::vec2(halfB.x, halfB.y);
    spec.twist = r.f("twist", 0.0f);
    spec.cellA = static_cast<int>(r.integer("cellA", 0));
    spec.cellB = static_cast<int>(r.integer("cellB", 1));
    const int64_t shape = r.integer("shape", 0);
    spec.look.shape = shape == 1 ? PortalShape::Ellipse : shape == 2 ? PortalShape::Rounded : PortalShape::Rect;
    spec.look.cornerRadius = r.f("corner", spec.look.cornerRadius);
    return spec;
}

PortalEnd endOf(const PairSpec& spec, bool first) {
    PortalEnd end;
    end.cell = "c" + std::to_string(first ? spec.cellA : spec.cellB);
    end.position = first ? spec.a : spec.b;
    end.eulerDegrees = first ? spec.ae : spec.be;
    end.halfSize = first ? spec.half : spec.halfB;
    return end;
}

void addCells(PortalSystem& system, int count) {
    for (int index = 0; index < count; ++index) {
        PortalCell cell;
        cell.name = "c" + std::to_string(index);
        system.addCell(cell);
    }
}

int cellCountOf(const std::vector<PairSpec>& pairs, int minimum = 2) {
    int count = minimum;
    for (const PairSpec& spec : pairs) count = std::max({count, spec.cellA + 1, spec.cellB + 1});
    return count;
}

void addPairSpec(PortalSystem& system, const PairSpec& spec, const std::string& name) { system.addPair(name, endOf(spec, true), endOf(spec, false), spec.twist, spec.look); }

float maxAbsDiff(const glm::mat4& left, const glm::mat4& right) {
    float worst = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            const float d = std::abs(left[column][row] - right[column][row]);
            worst = std::isfinite(d) ? std::max(worst, d) : 1.0e30f;
        }
    }
    return worst;
}

float maxAbsDiff3(const glm::mat4& left, const glm::mat4& right) {
    float worst = 0.0f;
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            const float d = std::abs(left[column][row] - right[column][row]);
            worst = std::isfinite(d) ? std::max(worst, d) : 1.0e30f;
        }
    }
    return worst;
}

float det3(const glm::mat4& m) { return glm::dot(glm::vec3(m[0]), glm::cross(glm::vec3(m[1]), glm::vec3(m[2]))); }

float orthoErr(const glm::mat4& m) {
    float worst = 0.0f;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) worst = std::max(worst, std::abs(glm::dot(glm::vec3(m[i]), glm::vec3(m[j])) - (i == j ? 1.0f : 0.0f)));
    }
    return worst;
}

void registerMatrix(Program& p) {
    reg(p, "xmMul", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xmMul");
        return matV(matOf(a[0], loc, "xmMul a") * matOf(a[1], loc, "xmMul b"));
    });
    reg(p, "xmInv", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xmInv");
        return matV(glm::inverse(matOf(a[0], loc, "xmInv")));
    });
    reg(p, "xmIdentity", [](const ValueList&, const SrcLoc&) { return matV(glm::mat4(1.0f)); });
    reg(p, "xmPoint", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xmPoint");
        return vecV(glm::vec3(matOf(a[0], loc, "xmPoint m") * glm::vec4(vec3Of(a[1], loc, "xmPoint p"), 1.0f)));
    });
    reg(p, "xmDir", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xmDir");
        return vecV(glm::vec3(matOf(a[0], loc, "xmDir m") * glm::vec4(vec3Of(a[1], loc, "xmDir d"), 0.0f)));
    });
    reg(p, "xmDet", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xmDet");
        return numV(det3(matOf(a[0], loc, "xmDet")));
    });
    reg(p, "xmOrtho", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xmOrtho");
        return numV(orthoErr(matOf(a[0], loc, "xmOrtho")));
    });
    reg(p, "xmDiff", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xmDiff");
        return numV(maxAbsDiff(matOf(a[0], loc, "xmDiff a"), matOf(a[1], loc, "xmDiff b")));
    });
    reg(p, "xmTranslation", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xmTranslation");
        return vecV(glm::vec3(matOf(a[0], loc, "xmTranslation")[3]));
    });
}

void registerPairs(Program& p) {
    reg(p, "xpPair", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xpPair");
        const PairSpec spec = readPair(a[0], loc);
        PortalSystem system;
        addCells(system, 2);
        PairSpec two = spec;
        two.cellA = 0;
        two.cellB = 1;
        addPairSpec(system, two, "p");
        const Portal& pa = system.portals()[0];
        const Portal& pb = system.portals()[1];
        return recV({{"ta", matV(pa.transform)},
                     {"tb", matV(pb.transform)},
                     {"poseA", matV(pa.pose)},
                     {"poseB", matV(pb.pose)},
                     {"centerA", vecV(pa.center())},
                     {"centerB", vecV(pb.center())},
                     {"normalA", vecV(pa.normal())},
                     {"normalB", vecV(pb.normal())},
                     {"rightA", vecV(pa.right())},
                     {"rightB", vecV(pb.right())},
                     {"upA", vecV(pa.up())},
                     {"upB", vecV(pb.up())},
                     {"partnerA", intV(pa.partner)},
                     {"partnerB", intV(pb.partner)}});
    });

    reg(p, "xpRoundTrip", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xpRoundTrip");
        PairSpec spec = readPair(a[0], loc);
        spec.cellA = 0;
        spec.cellB = 1;
        const float u = static_cast<float>(argNumber(a[1], loc, "u"));
        const float v = static_cast<float>(argNumber(a[2], loc, "v"));
        const Rec extra = rec(a[3], loc);
        const float front = extra.f("front", 0.5f);
        const float back = extra.f("back", 0.5f);
        PortalSystem system;
        addCells(system, 2);
        addPairSpec(system, spec, "p");
        const Portal& pa = system.portals()[0];
        const glm::vec3 lateral = pa.right() * (u * pa.half.x) + pa.up() * (v * pa.half.y);
        const glm::vec3 q0 = pa.center() + pa.normal() * front + lateral;
        glm::vec3 position = pa.center() - pa.normal() * back + lateral;
        const glm::vec3 q1 = position;
        int cell = 0;
        const int first = system.stepPoint(position, cell, q0);
        const glm::vec3 afterFirst = position;
        const int cellFirst = cell;
        glm::vec3 returning = glm::vec3(pa.transform * glm::vec4(q0, 1.0f));
        int second = -2;
        int cellSecond = cell;
        if (first >= 0) second = system.stepPoint(returning, cellSecond, afterFirst);
        return recV({{"first", intV(first)},
                     {"cellFirst", intV(cellFirst)},
                     {"second", intV(second)},
                     {"cellSecond", intV(cellSecond)},
                     {"start", vecV(q0)},
                     {"end", vecV(returning)},
                     {"mapped", vecV(afterFirst)},
                     {"expectedMapped", vecV(glm::vec3(pa.transform * glm::vec4(q1, 1.0f)))}});
    });

    reg(p, "xpRing", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xpRing");
        std::vector<PairSpec> pairs;
        for (const Value& item : argList(a[0], loc, "pairs")) pairs.push_back(readPair(item, loc));
        const int n = static_cast<int>(pairs.size());
        PortalSystem system;
        addCells(system, std::max(n, 1));
        for (int i = 0; i < n; ++i) {
            pairs[static_cast<size_t>(i)].cellA = i;
            pairs[static_cast<size_t>(i)].cellB = (i + 1) % n;
            addPairSpec(system, pairs[static_cast<size_t>(i)], "r" + std::to_string(i));
        }
        glm::mat4 forward(1.0f);
        glm::mat4 backward(1.0f);
        for (int i = 0; i < n; ++i) forward = system.portals()[static_cast<size_t>(2 * i)].transform * forward;
        for (int i = n - 1; i >= 0; --i) backward = system.portals()[static_cast<size_t>(2 * i + 1)].transform * backward;
        const glm::mat4 round = backward * forward;
        const glm::mat4 flip = glm::rotate(glm::mat4(1.0f), kPi, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 direct(1.0f);
        for (int i = 0; i < n; ++i) {
            const PairSpec& s = pairs[static_cast<size_t>(i)];
            const glm::mat4 twist = glm::rotate(glm::mat4(1.0f), glm::radians(s.twist), glm::vec3(0.0f, 0.0f, 1.0f));
            direct = portalPose(s.b, s.be) * twist * flip * glm::inverse(portalPose(s.a, s.ae)) * direct;
        }
        return recV({{"roundErr", numV(maxAbsDiff(round, glm::mat4(1.0f)))}, {"forwardOrtho", numV(orthoErr(forward))}, {"forwardDet", numV(det3(forward))}, {"directErr", numV(maxAbsDiff3(direct, forward))}, {"forward", matV(forward)}});
    });
}

Camera makeCamera(const glm::vec3& position, float yaw, float pitch, float roll) {
    Camera camera(position);
    camera.setOrientation(yaw, pitch, roll);
    return camera;
}

Camera aimedCamera(const glm::vec3& position, const glm::vec3& target, float roll, float yawOffset, float pitchOffset) {
    const glm::vec3 d = glm::normalize(target - position);
    const float yaw = glm::degrees(std::atan2(d.z, d.x)) + yawOffset;
    const float pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f))) + pitchOffset;
    return makeCamera(position, yaw, pitch, roll);
}

void registerCrossing(Program& p) {
    reg(p, "xpCrossView", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xpCrossView");
        PairSpec spec = readPair(a[0], loc);
        spec.cellA = 0;
        spec.cellB = 1;
        const Rec c = rec(a[1], loc);
        PortalSystem system;
        addCells(system, 2);
        addPairSpec(system, spec, "p");
        system.settings().maxDepth = 1;
        system.settings().minPixels = 0.0f;
        const Portal& pa = system.portals()[0];
        const float dist = c.f("dist", 3.0f);
        const glm::vec3 position = pa.center() + pa.normal() * dist + pa.right() * (c.f("u", 0.0f) * pa.half.x) + pa.up() * (c.f("v", 0.0f) * pa.half.y);
        Camera camera = aimedCamera(position, pa.center(), c.f("roll", 0.0f), c.f("yawOff", 0.0f), c.f("pitchOff", 0.0f));
        system.setCameraCell(0);
        const PortalFrame frame = system.buildFrame(camera, 1080.0f);
        int child = -1;
        if (!frame.nodes.empty()) {
            for (const PortalSurface& surface : frame.nodes[0].surfaces) {
                if (surface.child >= 0 && glm::length(surface.center - pa.center()) < 1.0e-3f) child = surface.child;
            }
        }
        Fields out;
        out.emplace_back("have", boolV(child >= 0));
        glm::mat4 direct(1.0f);
        Camera swapped = camera;
        swapped.transformBy(pa.transform);
        const glm::mat4 viewAfter = swapped.getViewMatrix();
        float viewErr = 1.0e30f;
        if (child >= 0) viewErr = maxAbsDiff(frame.nodes[static_cast<size_t>(child)].view, viewAfter);
        out.emplace_back("viewErr", numV(viewErr));
        out.emplace_back("posErr", numV(glm::length(swapped.getPosition() - glm::vec3(pa.transform * glm::vec4(position, 1.0f)))));
        const glm::vec3 expectedFront = glm::normalize(glm::vec3(pa.transform * glm::vec4(camera.getFront(), 0.0f)));
        const glm::vec3 expectedUp = glm::normalize(glm::vec3(pa.transform * glm::vec4(camera.getUp(), 0.0f)));
        out.emplace_back("frontErr", numV(glm::length(swapped.getFront() - expectedFront)));
        out.emplace_back("upErr", numV(glm::length(swapped.getUp() - expectedUp)));
        out.emplace_back("rightOrtho", numV(std::abs(glm::dot(swapped.getRight(), swapped.getFront())) + std::abs(glm::dot(swapped.getRight(), swapped.getUp())) + std::abs(glm::dot(swapped.getFront(), swapped.getUp()))));

        Camera walker = aimedCamera(position, pa.center(), c.f("roll", 0.0f), c.f("yawOff", 0.0f), c.f("pitchOff", 0.0f));
        const glm::vec3 direction = glm::normalize(pa.center() - position);
        const float cosine = -glm::dot(direction, pa.normal());
        bool crossed = false;
        float rotErr = 1.0e30f;
        float posErr = 1.0e30f;
        if (cosine > 0.2f) {
            const glm::vec3 front = walker.getFront();
            const float cosFront = -glm::dot(front, pa.normal());
            if (cosFront > 0.2f) {
                const glm::vec3 nearTarget = pa.center() + pa.normal() * 0.01f + pa.right() * (c.f("u", 0.0f) * pa.half.x * 0.5f) + pa.up() * (c.f("v", 0.0f) * pa.half.y * 0.5f);
                walker.setPos(nearTarget - front * walker.getNearPlane());
                system.setCameraCell(0);
                const int idle = system.step(walker);
                const glm::vec3 before = walker.getPosition();
                const glm::vec3 beforeFront = walker.getFront();
                const glm::vec3 beforeUp = walker.getUp();
                walker.setPos(before + front * (0.03f / cosFront));
                const glm::vec3 moved = walker.getPosition();
                const int id = system.step(walker);
                crossed = idle < 0 && id == 0;
                if (crossed) {
                    rotErr = std::max(glm::length(walker.getFront() - glm::normalize(glm::vec3(pa.transform * glm::vec4(beforeFront, 0.0f)))), glm::length(walker.getUp() - glm::normalize(glm::vec3(pa.transform * glm::vec4(beforeUp, 0.0f)))));
                    posErr = glm::length(walker.getPosition() - glm::vec3(pa.transform * glm::vec4(moved, 1.0f)));
                }
            }
        }
        out.emplace_back("crossed", boolV(crossed));
        out.emplace_back("stepRotErr", numV(rotErr));
        out.emplace_back("stepPosErr", numV(posErr));
        return recV(std::move(out));
    });
}

struct FrameInput {
    std::vector<PairSpec> pairs;
    int cells = 2;
    glm::vec3 pos{0.0f, 1.6f, 6.0f};
    float yaw = -90.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    int cameraCell = 0;
    int maxDepth = 3;
    int maxViews = 24;
    float viewport = 1080.0f;
    float minPixels = 6.0f;
    bool occlusion = true;
    bool blockAll = false;
    float fov = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

FrameInput readFrameInput(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    FrameInput in;
    for (const Value& item : r.list("pairs")) in.pairs.push_back(readPair(item, loc));
    in.cells = cellCountOf(in.pairs, static_cast<int>(r.integer("cells", 2)));
    in.pos = r.vec("pos", in.pos);
    in.yaw = r.f("yaw", in.yaw);
    in.pitch = r.f("pitch", in.pitch);
    in.roll = r.f("roll", in.roll);
    in.cameraCell = static_cast<int>(r.integer("cell", 0));
    in.maxDepth = static_cast<int>(r.integer("maxDepth", 3));
    in.maxViews = static_cast<int>(r.integer("maxViews", 24));
    in.viewport = r.f("viewport", 1080.0f);
    in.minPixels = r.f("minPixels", 6.0f);
    in.occlusion = r.flag("occlusion", true);
    in.blockAll = r.flag("blockAll", false);
    in.fov = r.f("fov", 45.0f);
    in.nearPlane = r.f("near", 0.1f);
    in.farPlane = r.f("far", 100.0f);
    return in;
}

struct BuiltFrame {
    PortalSystem system;
    PortalFrame frame;
    Camera camera;
};

std::unique_ptr<BuiltFrame> buildFrameFor(const FrameInput& in) {
    auto built = std::make_unique<BuiltFrame>();
    addCells(built->system, in.cells);
    for (size_t i = 0; i < in.pairs.size(); ++i) addPairSpec(built->system, in.pairs[i], "f" + std::to_string(i));
    built->system.settings().maxDepth = in.maxDepth;
    built->system.settings().maxViews = in.maxViews;
    built->system.settings().minPixels = in.minPixels;
    built->system.settings().occlusion = in.occlusion;
    built->camera = makeCamera(in.pos, in.yaw, in.pitch, in.roll);
    built->camera.setFov(in.fov);
    built->camera.setNearPlane(in.nearPlane);
    built->camera.setFarPlane(in.farPlane);
    built->system.setCameraCell(in.cameraCell);
    PortalBlockedFn blocked;
    if (in.blockAll) blocked = [](const glm::vec3&, const glm::vec3&) { return true; };
    built->frame = built->system.buildFrame(built->camera, in.viewport, blocked);
    return built;
}

std::array<glm::vec3, 4> cornersOfPortal(const Portal& portal) {
    const glm::vec3 center = portal.center();
    const glm::vec3 h = portal.right() * portal.half.x;
    const glm::vec3 v = portal.up() * portal.half.y;
    return {center - h - v, center + h - v, center + h + v, center - h + v};
}

Value frameNodeInfo(const BuiltFrame& built, const FrameInput& in) {
    const PortalFrame& frame = built.frame;
    const PortalSystem& system = built.system;
    ValueList nodes;
    const size_t count = frame.nodes.size();
    std::vector<int> parent(count, -1);
    std::vector<int> viaPortal(count, -1);
    std::vector<int> parentSurface(count, -1);
    for (size_t n = 0; n < count; ++n) {
        for (size_t s = 0; s < frame.nodes[n].surfaces.size(); ++s) {
            const int child = frame.nodes[n].surfaces[s].child;
            if (child < 0 || static_cast<size_t>(child) >= count) continue;
            parent[static_cast<size_t>(child)] = static_cast<int>(n);
            parentSurface[static_cast<size_t>(child)] = static_cast<int>(s);
            for (int id : system.portalsOf(frame.nodes[n].cell)) {
                const Portal& portal = system.portals()[static_cast<size_t>(id)];
                if (glm::length(portal.center() - frame.nodes[n].surfaces[s].center) < 1.0e-3f && glm::length(portal.right() * portal.half.x - frame.nodes[n].surfaces[s].axisU) < 1.0e-3f) viaPortal[static_cast<size_t>(child)] = id;
            }
        }
    }
    for (size_t n = 0; n < count; ++n) {
        const PortalViewNode& node = frame.nodes[n];
        Fields f;
        f.emplace_back("depth", intV(node.depth));
        f.emplace_back("cell", intV(node.cell));
        f.emplace_back("parent", intV(parent[n]));
        f.emplace_back("portal", intV(viaPortal[n]));
        f.emplace_back("key", intV(static_cast<int64_t>(node.key)));
        f.emplace_back("masked", boolV(node.masked));
        f.emplace_back("surfaces", intV(static_cast<int64_t>(node.surfaces.size())));
        f.emplace_back("label", strV(node.label));
        f.emplace_back("view", matV(node.view));
        int stray = 0;
        for (const PortalSurface& surface : node.surfaces) {
            bool found = false;
            for (int id : system.portalsOf(node.cell)) found = found || glm::length(system.portals()[static_cast<size_t>(id)].center() - surface.center) < 1.0e-3f;
            stray += found ? 0 : 1;
        }
        f.emplace_back("strayOpenings", intV(stray));
        float chainErr = 0.0f;
        float cellErr = 0.0f;
        float planeErr = -1.0f;
        float frontMax = -1.0e30f;
        float eyeMin = 1.0e30f;
        int samples = 0;
        int farSamples = 0;
        int eyeSamples = 0;
        bool oblique = false;
        float maskErr = -1.0f;
        float maskInside = 1.0f;
        if (n > 0 && viaPortal[n] >= 0 && parent[n] >= 0) {
            std::vector<int> chain;
            for (int cursor = static_cast<int>(n); cursor > 0; cursor = parent[static_cast<size_t>(cursor)]) {
                if (viaPortal[static_cast<size_t>(cursor)] < 0 || parent[static_cast<size_t>(cursor)] < 0) {
                    chain.clear();
                    break;
                }
                chain.push_back(viaPortal[static_cast<size_t>(cursor)]);
            }
            glm::mat4 world = glm::inverse(frame.nodes[0].view);
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) world = system.portals()[static_cast<size_t>(*it)].transform * world;
            if (!chain.empty()) chainErr = maxAbsDiff(frame.nodes[n].view, glm::inverse(world));
            const Portal& source = system.portals()[static_cast<size_t>(viaPortal[n])];
            const Portal& destination = system.portals()[static_cast<size_t>(source.partner)];
            cellErr = destination.cell == node.cell ? 0.0f : 1.0f;
            oblique = maxAbsDiff(node.projection, frame.nodes[0].projection) > 1.0e-6f;
            const glm::mat4 vp = node.projection * node.view;
            const glm::vec3 eye = node.position;
            const float eyeDistance = glm::dot(destination.normal(), eye - destination.center());
            if (oblique) {
                planeErr = 0.0f;
                const std::array<glm::vec2, 5> offsets = {glm::vec2(0.0f), glm::vec2(0.5f, 0.5f), glm::vec2(-0.5f, 0.5f), glm::vec2(0.5f, -0.5f), glm::vec2(-0.5f, -0.5f)};
                for (const glm::vec2& o : offsets) {
                    const glm::vec3 onPlane = destination.center() + destination.right() * (o.x * destination.half.x) + destination.up() * (o.y * destination.half.y);
                    const glm::vec3 beyond = onPlane + destination.normal() * 1.0f;
                    const glm::vec3 eyeSide = onPlane - destination.normal() * std::max(std::abs(eyeDistance) * 0.5f, 0.05f);
                    const glm::vec4 c0 = vp * glm::vec4(onPlane, 1.0f);
                    const glm::vec4 c1 = vp * glm::vec4(beyond, 1.0f);
                    const glm::vec4 c2 = vp * glm::vec4(eyeSide, 1.0f);
                    if (c0.w > 1.0e-3f) {
                        planeErr = std::max(planeErr, std::abs(c0.z / c0.w - 1.0f));
                        ++samples;
                    }
                    if (c1.w > 1.0e-3f) {
                        frontMax = std::max(frontMax, c1.z / c1.w);
                        ++farSamples;
                    }
                    if (c2.w > 1.0e-3f) {
                        eyeMin = std::min(eyeMin, c2.z / c2.w);
                        ++eyeSamples;
                    }
                }
            }
            if (node.masked) {
                maskErr = 0.0f;
                const std::array<glm::vec3, 4> corners = cornersOfPortal(destination);
                for (size_t side = 0; side < 4; ++side) {
                    const glm::vec4 plane = node.planes[side];
                    maskErr = std::max({maskErr, std::abs(glm::dot(glm::vec3(plane), corners[side]) + plane.w), std::abs(glm::dot(glm::vec3(plane), corners[(side + 1) % 4]) + plane.w), std::abs(glm::dot(glm::vec3(plane), eye) + plane.w)});
                    if (glm::dot(glm::vec3(plane), destination.center()) + plane.w < -1.0e-3f) maskInside = 0.0f;
                }
            }
        }
        f.emplace_back("chainErr", numV(chainErr));
        f.emplace_back("cellErr", numV(cellErr));
        f.emplace_back("oblique", boolV(oblique));
        f.emplace_back("planeErr", numV(planeErr));
        f.emplace_back("planeSamples", intV(samples));
        f.emplace_back("frontMax", numV(frontMax));
        f.emplace_back("frontSamples", intV(farSamples));
        f.emplace_back("eyeMin", numV(eyeMin));
        f.emplace_back("eyeSamples", intV(eyeSamples));
        f.emplace_back("maskErr", numV(maskErr));
        f.emplace_back("maskInside", numV(maskInside));
        nodes.push_back(recV(std::move(f)));
    }
    (void)in;
    return listV(std::move(nodes));
}

void registerFrame(Program& p) {
    reg(p, "xpFrame", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xpFrame");
        const FrameInput in = readFrameInput(a[0], loc);
        const std::unique_ptr<BuiltFrame> built = buildFrameFor(in);
        Fields out;
        out.emplace_back("count", intV(static_cast<int64_t>(built->frame.nodes.size())));
        out.emplace_back("empty", boolV(built->frame.empty()));
        out.emplace_back("rootErr", numV(built->frame.nodes.empty() ? 0.0 : maxAbsDiff(built->frame.nodes[0].view, built->camera.getViewMatrix())));
        out.emplace_back("rootProjErr", numV(built->frame.nodes.empty() ? 0.0 : maxAbsDiff(built->frame.nodes[0].projection, built->camera.getProjectionMatrix())));
        out.emplace_back("nodes", frameNodeInfo(*built, in));
        return recV(std::move(out));
    });
    reg(p, "xpFrameTwice", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xpFrameTwice");
        const FrameInput in = readFrameInput(a[0], loc);
        const auto first = buildFrameFor(in);
        const auto second = buildFrameFor(in);
        bool same = first->frame.nodes.size() == second->frame.nodes.size();
        for (size_t n = 0; same && n < first->frame.nodes.size(); ++n) {
            same = first->frame.nodes[n].key == second->frame.nodes[n].key && maxAbsDiff(first->frame.nodes[n].view, second->frame.nodes[n].view) == 0.0f && first->frame.nodes[n].label == second->frame.nodes[n].label && first->frame.nodes[n].surfaces.size() == second->frame.nodes[n].surfaces.size();
        }
        return boolV(same);
    });
}

struct SphereSpec {
    glm::vec3 position{0.0f};
    int mesh = 0;
};

float meshRadius(int mesh) { return 0.5f + 0.5f * static_cast<float>(mesh % 4); }

struct QueryWorld {
    World world;
    PortalSystem system;
    std::vector<EntityHandle> entities;
    std::vector<SphereSpec> spheres;

    QueryWorld(const std::vector<PairSpec>& pairs, int cells, const ValueList& sphereList, const SrcLoc& loc) {
        world.setMeshBoundsHook([](uint32_t id) { return glm::vec4(0.0f, 0.0f, 0.0f, id == NO_MESH ? 0.0f : 0.5f + 0.5f * static_cast<float>(id % 4)); });
        addCells(system, cells);
        for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(system, pairs[i], "q" + std::to_string(i));
        for (const Value& item : sphereList) {
            const Rec s = rec(item, loc);
            SphereSpec sphere;
            sphere.position = s.vec("pos", glm::vec3(0.0f));
            sphere.mesh = static_cast<int>(s.integer("mesh", 0));
            EntityDesc desc;
            desc.position = sphere.position;
            desc.mesh = static_cast<uint32_t>(sphere.mesh);
            entities.push_back(world.create(desc));
            spheres.push_back(sphere);
        }
    }

    [[nodiscard]] int indexOf(EntityHandle handle) const {
        for (size_t i = 0; i < entities.size(); ++i) {
            if (entities[i] == handle) return static_cast<int>(i);
        }
        return -1;
    }
};

float raySphere(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& center, float radius) {
    const glm::vec3 oc = center - origin;
    const float along = glm::dot(oc, direction);
    const float perpendicular2 = glm::dot(oc, oc) - along * along;
    if (perpendicular2 > radius * radius) return -1.0f;
    const float half = std::sqrt(radius * radius - perpendicular2);
    float d = along - half;
    if (d < 0.0f) d = along + half;
    return d < 0.0f ? -1.0f : d;
}

void registerQueries(Program& p) {
    reg(p, "xpRaycast", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xpRaycast");
        const std::vector<PairSpec> pairs = {[&] {
            PairSpec s = readPair(a[0], loc);
            s.cellA = 0;
            s.cellB = 1;
            return s;
        }()};
        QueryWorld scene(pairs, 2, argList(a[1], loc, "spheres"), loc);
        const glm::vec3 origin = vec3Of(a[2], loc, "origin");
        const Rec r = rec(a[3], loc);
        const glm::vec3 direction = glm::normalize(r.vec("dir", glm::vec3(0.0f, 0.0f, -1.0f)));
        const float maxDistance = r.f("max", 100.0f);
        const int startCell = static_cast<int>(r.integer("cell", 0));
        const PortalRayHit hit = scene.system.raycast(scene.world, origin, direction, maxDistance, startCell, static_cast<int>(r.integer("maxCrossings", 8)));
        const Portal& entry = scene.system.portals()[static_cast<size_t>(startCell == 0 ? 0 : 1)];
        float portalDistance = -1.0f;
        {
            const float denominator = glm::dot(entry.normal(), direction);
            if (denominator < -1.0e-6f) {
                const float d = glm::dot(entry.normal(), entry.center() - origin) / denominator;
                if (d >= 0.0f && d <= maxDistance) {
                    const glm::vec3 offset = origin + direction * d - entry.center();
                    if (entry.inside(glm::dot(offset, entry.right()), glm::dot(offset, entry.up()))) portalDistance = d;
                }
            }
        }
        int bestIndex = -1;
        float bestDistance = 1.0e30f;
        int bestCrossings = 0;
        const float directLimit = portalDistance >= 0.0f ? portalDistance : maxDistance;
        for (size_t i = 0; i < scene.spheres.size(); ++i) {
            const float d = raySphere(origin, direction, scene.spheres[i].position, meshRadius(scene.spheres[i].mesh));
            if (d >= 0.0f && d <= directLimit && d < bestDistance) {
                bestDistance = d;
                bestIndex = static_cast<int>(i);
            }
        }
        if (bestIndex < 0 && portalDistance >= 0.0f) {
            const glm::vec3 entryPoint = origin + direction * portalDistance;
            const glm::vec3 newOrigin = glm::vec3(entry.transform * glm::vec4(entryPoint, 1.0f));
            const glm::vec3 newDirection = glm::normalize(glm::vec3(entry.transform * glm::vec4(direction, 0.0f)));
            const float used = portalDistance + 1.0e-3f;
            for (size_t i = 0; i < scene.spheres.size(); ++i) {
                const float d = raySphere(newOrigin + newDirection * 1.0e-3f, newDirection, scene.spheres[i].position, meshRadius(scene.spheres[i].mesh));
                if (d >= 0.0f && d + used <= maxDistance && used + d < bestDistance) {
                    bestDistance = used + d;
                    bestIndex = static_cast<int>(i);
                    bestCrossings = 1;
                }
            }
        }
        if (bestIndex < 0 && portalDistance >= 0.0f) bestCrossings = 1;
        return recV({{"hit", intV(hit.hit ? scene.indexOf(hit.hit->entity) : -1)},
                     {"distance", numV(hit.distance)},
                     {"crossings", intV(hit.crossings)},
                     {"cell", intV(hit.cell)},
                     {"point", vecV(hit.point)},
                     {"direction", vecV(hit.direction)},
                     {"refHit", intV(bestIndex)},
                     {"refDistance", numV(bestIndex >= 0 ? bestDistance : -1.0)},
                     {"refCrossings", intV(bestCrossings)},
                     {"portalDistance", numV(portalDistance)}});
    });

    reg(p, "xpOverlap", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xpOverlap");
        PairSpec spec = readPair(a[0], loc);
        spec.cellA = 0;
        spec.cellB = 1;
        QueryWorld scene({spec}, 2, argList(a[1], loc, "spheres"), loc);
        const glm::vec3 center = vec3Of(a[2], loc, "center");
        const float radius = static_cast<float>(argNumber(a[3], loc, "radius"));
        const std::vector<PortalOverlap> found = scene.system.overlapSphere(scene.world, center, radius, 0);
        ValueList list;
        for (const PortalOverlap& overlap : found) list.push_back(recV({{"index", intV(scene.indexOf(overlap.entity))}, {"crossings", intV(overlap.crossings)}}));
        return listV(std::move(list));
    });

    reg(p, "xpTwin", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xpTwin");
        PairSpec spec = readPair(a[0], loc);
        spec.cellA = 0;
        spec.cellB = 1;
        const Rec r = rec(a[1], loc);
        World world;
        world.setMeshBoundsHook([](uint32_t id) { return glm::vec4(0.0f, 0.0f, 0.0f, id == NO_MESH ? 0.0f : 0.5f + 0.5f * static_cast<float>(id % 4)); });
        PortalSystem system;
        addCells(system, 2);
        addPairSpec(system, spec, "t");
        const Portal& pa = system.portals()[0];
        const int mesh = static_cast<int>(r.integer("mesh", 0));
        const float radius = meshRadius(mesh);
        const glm::vec3 lateral = pa.right() * (r.f("u", 0.0f) * pa.half.x) + pa.up() * (r.f("v", 0.0f) * pa.half.y);
        EntityDesc desc;
        desc.position = pa.center() + pa.normal() * (radius * 3.0f) + lateral;
        desc.mesh = static_cast<uint32_t>(mesh);
        const EntityHandle entity = world.create(desc);
        system.track(world, entity);
        system.updateObjects(world);
        Fields out;
        const bool noGhostFar = !world.isAlive(system.ghostOf(entity)) || !world.isVisible(system.ghostOf(entity));
        world.setWorldMatrix(entity, glm::translate(glm::mat4(1.0f), pa.center() + pa.normal() * (radius * 0.4f) + lateral));
        system.updateObjects(world);
        const EntityHandle ghost = system.ghostOf(entity);
        const bool ghostAlive = world.isAlive(ghost);
        float ghostErr = 1.0e30f;
        bool ghostVisible = false;
        if (ghostAlive) {
            ghostErr = maxAbsDiff(world.getWorldMatrix(ghost), pa.transform * world.getWorldMatrix(entity));
            ghostVisible = world.isVisible(ghost);
        }
        const bool cellBefore = system.objectCell(entity) == 0;
        const glm::mat4 beforeCross = world.getWorldMatrix(entity);
        world.setWorldMatrix(entity, glm::translate(glm::mat4(1.0f), pa.center() - pa.normal() * (radius * 0.4f) + lateral));
        const glm::mat4 moved = world.getWorldMatrix(entity);
        system.updateObjects(world);
        const float crossErr = maxAbsDiff(world.getWorldMatrix(entity), pa.transform * moved);
        const int cellAfter = system.objectCell(entity);
        (void)beforeCross;
        world.setWorldMatrix(entity, glm::translate(glm::mat4(1.0f), glm::vec3(world.getWorldMatrix(entity)[3]) + glm::vec3(0.0f, 60.0f, 0.0f)));
        system.updateObjects(world);
        const EntityHandle ghostFar = system.ghostOf(entity);
        const bool hiddenFar = !world.isAlive(ghostFar) || !world.isVisible(ghostFar);
        out.emplace_back("noGhostFar", boolV(noGhostFar));
        out.emplace_back("ghostAlive", boolV(ghostAlive));
        out.emplace_back("ghostVisible", boolV(ghostVisible));
        out.emplace_back("ghostErr", numV(ghostErr));
        out.emplace_back("cellBefore", boolV(cellBefore));
        out.emplace_back("crossErr", numV(crossErr));
        out.emplace_back("cellAfter", intV(cellAfter));
        out.emplace_back("hiddenFar", boolV(hiddenFar));
        out.emplace_back("isGhost", boolV(ghostAlive && system.isGhost(ghost)));
        return recV(std::move(out));
    });
}

struct CellSpec {
    glm::vec3 gravity{0.0f, -9.81f, 0.0f};
    glm::vec3 tint{0.1f};
    float sun = 1.0f;
    float ambient = 1.0f;
    float scalar = 0.0f;
};

void registerCells(Program& p) {
    reg(p, "xpSample", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xpSample");
        const Rec r = rec(a[0], loc);
        std::vector<PairSpec> pairs;
        for (const Value& item : r.list("pairs")) pairs.push_back(readPair(item, loc));
        const ValueList& cellValues = r.list("cells");
        PortalSystem system;
        for (size_t i = 0; i < cellValues.size(); ++i) {
            const Rec c = rec(cellValues[i], loc);
            PortalCell cell;
            cell.name = "c" + std::to_string(i);
            cell.params.gravity = c.vec("gravity", cell.params.gravity);
            cell.params.tint = c.vec("tint", cell.params.tint);
            cell.params.sun = c.f("sun", 1.0f);
            cell.params.ambient = c.f("ambient", 1.0f);
            cell.params.scalars["s"] = c.f("scalar", 0.0f);
            system.addCell(cell);
        }
        for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(system, pairs[i], "s" + std::to_string(i));
        system.settings().blendDistance = r.f("blend", 2.0f);
        const PortalCellParams out = system.sampleParams(static_cast<int>(argInt(a[1], loc, "cell")), vec3Of(a[2], loc, "pos"));
        const auto it = out.scalars.find("s");
        return recV({{"gravity", vecV(out.gravity)}, {"tint", vecV(out.tint)}, {"sun", numV(out.sun)}, {"ambient", numV(out.ambient)}, {"scalar", numV(it != out.scalars.end() ? it->second : 0.0)}});
    });

    reg(p, "xpLightImages", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xpLightImages");
        const Rec r = rec(a[0], loc);
        std::vector<PairSpec> pairs;
        for (const Value& item : r.list("pairs")) pairs.push_back(readPair(item, loc));
        PortalSystem system;
        addCells(system, cellCountOf(pairs));
        for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(system, pairs[i], "l" + std::to_string(i));
        system.settings().lightTransport = r.flag("transport", true);
        system.settings().maxDepth = static_cast<int>(r.integer("maxDepth", 3));
        const int cell = static_cast<int>(argInt(a[1], loc, "cell"));
        const std::vector<PortalLightImage> images = system.lightImages(cell, vec3Of(a[2], loc, "pos"), static_cast<float>(argNumber(a[3], loc, "range")));
        ValueList list;
        for (const PortalLightImage& image : images) {
            list.push_back(recV({{"portal", intV(image.portal)}, {"cell", intV(image.cell)}, {"transform", matV(image.transform)}, {"center", vecV(image.center)}, {"normal", vecV(image.normal)}, {"axisU", vecV(image.axisU)}, {"axisV", vecV(image.axisV)}}));
        }
        ValueList portals;
        for (const Portal& portal : system.portals()) {
            portals.push_back(recV({{"cell", intV(portal.cell)}, {"partner", intV(portal.partner)}, {"center", vecV(portal.center())}, {"normal", vecV(portal.normal())}, {"right", vecV(portal.right())}, {"up", vecV(portal.up())}, {"half", vecV(glm::vec3(portal.half, 0.0f))}, {"transform", matV(portal.transform)}}));
        }
        return recV({{"images", listV(std::move(list))}, {"portals", listV(std::move(portals))}});
    });
}

std::string jsonOf(const std::vector<PairSpec>& pairs, const ValueList& cellValues, const SrcLoc& loc, int minCells = 2) {
    PortalSystem system;
    const int count = std::max<int>(static_cast<int>(cellValues.size()), cellCountOf(pairs, minCells));
    for (int i = 0; i < count; ++i) {
        PortalCell cell;
        cell.name = "c" + std::to_string(i);
        if (static_cast<size_t>(i) < cellValues.size()) {
            const Rec c = rec(cellValues[static_cast<size_t>(i)], loc);
            cell.params.gravity = c.vec("gravity", cell.params.gravity);
            cell.params.tint = c.vec("tint", cell.params.tint);
            cell.params.sun = c.f("sun", 1.0f);
            cell.params.ambient = c.f("ambient", 1.0f);
            if (c.has("scalar")) cell.params.scalars["k"] = c.f("scalar", 0.0f);
            cell.min = c.vec("min", cell.min);
            cell.max = c.vec("max", cell.max);
        }
        system.addCell(cell);
    }
    for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(system, pairs[i], "j" + std::to_string(i));
    return system.toJson();
}

void registerJsonShapes(Program& p) {
    reg(p, "xpJsonText", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xpJsonText");
        std::vector<PairSpec> pairs;
        for (const Value& item : argList(a[0], loc, "pairs")) pairs.push_back(readPair(item, loc));
        return strV(jsonOf(pairs, argList(a[1], loc, "cells"), loc));
    });
    reg(p, "xpJsonRoundTrip", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xpJsonRoundTrip");
        std::vector<PairSpec> pairs;
        for (const Value& item : argList(a[0], loc, "pairs")) pairs.push_back(readPair(item, loc));
        const ValueList& cells = argList(a[1], loc, "cells");
        PortalSystem original;
        const int count = std::max<int>(static_cast<int>(cells.size()), cellCountOf(pairs));
        for (int i = 0; i < count; ++i) {
            PortalCell cell;
            cell.name = "c" + std::to_string(i);
            if (static_cast<size_t>(i) < cells.size()) {
                const Rec c = rec(cells[static_cast<size_t>(i)], loc);
                cell.params.gravity = c.vec("gravity", cell.params.gravity);
                cell.params.tint = c.vec("tint", cell.params.tint);
                cell.params.sun = c.f("sun", 1.0f);
                cell.params.ambient = c.f("ambient", 1.0f);
                if (c.has("scalar")) cell.params.scalars["k"] = c.f("scalar", 0.0f);
            }
            original.addCell(cell);
        }
        for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(original, pairs[i], "j" + std::to_string(i));
        const std::string first = original.toJson();
        PortalSystem loaded;
        const bool ok = loaded.parseJson(first);
        const std::string second = loaded.toJson();
        float transformErr = 0.0f;
        bool sameShape = ok && loaded.portals().size() == original.portals().size() && loaded.cells().size() == original.cells().size();
        if (sameShape) {
            for (size_t i = 0; i < original.portals().size(); ++i) {
                transformErr = std::max(transformErr, maxAbsDiff(original.portals()[i].transform, loaded.portals()[i].transform));
                sameShape = sameShape && original.portals()[i].partner == loaded.portals()[i].partner && original.portals()[i].cell == loaded.portals()[i].cell && original.portals()[i].look.shape == loaded.portals()[i].look.shape;
            }
        }
        float paramErr = 0.0f;
        if (ok && loaded.cells().size() == original.cells().size()) {
            for (size_t i = 0; i < original.cells().size(); ++i) {
                paramErr = std::max({paramErr, glm::length(original.cells()[i].params.gravity - loaded.cells()[i].params.gravity), glm::length(original.cells()[i].params.tint - loaded.cells()[i].params.tint), std::abs(original.cells()[i].params.sun - loaded.cells()[i].params.sun)});
            }
        }
        return recV({{"ok", boolV(ok)}, {"fixpoint", boolV(first == second)}, {"sameShape", boolV(sameShape)}, {"transformErr", numV(transformErr)}, {"paramErr", numV(paramErr)}, {"portals", intV(static_cast<int64_t>(original.portals().size()))}});
    });
    reg(p, "xpJsonTry", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xpJsonTry");
        std::vector<PairSpec> pairs;
        for (const Value& item : argList(a[0], loc, "pairs")) pairs.push_back(readPair(item, loc));
        PortalSystem system;
        addCells(system, cellCountOf(pairs));
        for (size_t i = 0; i < pairs.size(); ++i) addPairSpec(system, pairs[i], "k" + std::to_string(i));
        const std::string before = system.toJson();
        const bool ok = system.parseJson(argString(a[1], loc, "text"));
        const std::string after = system.toJson();
        bool consistent = true;
        float worstOrtho = 0.0f;
        for (size_t i = 0; i < system.portals().size(); ++i) {
            const Portal& portal = system.portals()[i];
            consistent = consistent && portal.partner >= 0 && static_cast<size_t>(portal.partner) < system.portals().size() && system.portals()[static_cast<size_t>(portal.partner)].partner == static_cast<int>(i) && portal.cell >= 0 && static_cast<size_t>(portal.cell) < system.cells().size();
            if (std::isfinite(portal.transform[0][0])) worstOrtho = std::max(worstOrtho, orthoErr(portal.transform));
        }
        return recV({{"ok", boolV(ok)}, {"unchanged", boolV(before == after)}, {"consistent", boolV(consistent)}, {"ortho", numV(worstOrtho)}, {"portals", intV(static_cast<int64_t>(system.portals().size()))}});
    });

    reg(p, "xpShapeDistance", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 6, loc, "xpShapeDistance");
        const int64_t shape = argInt(a[0], loc, "shape");
        const PortalShape kind = shape == 1 ? PortalShape::Ellipse : shape == 2 ? PortalShape::Rounded : PortalShape::Rect;
        const glm::vec2 half(static_cast<float>(argNumber(a[1], loc, "hx")), static_cast<float>(argNumber(a[2], loc, "hy")));
        return numV(portalShapeDistance(kind, half, static_cast<float>(argNumber(a[3], loc, "r")), static_cast<float>(argNumber(a[4], loc, "u")), static_cast<float>(argNumber(a[5], loc, "v"))));
    });

    reg(p, "xpFrameMesh", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xpFrameMesh");
        const Rec r = rec(a[0], loc);
        PortalLook look;
        const int64_t shape = r.integer("shape", 0);
        look.shape = shape == 1 ? PortalShape::Ellipse : shape == 2 ? PortalShape::Rounded : PortalShape::Rect;
        look.cornerRadius = r.f("corner", look.cornerRadius);
        look.frame.width = r.f("width", look.frame.width);
        look.frame.depth = r.f("depth", look.frame.depth);
        look.frame.back = r.f("back", look.frame.back);
        look.frame.bevel = r.f("bevel", look.frame.bevel);
        const glm::vec3 half = r.vec("half", glm::vec3(1.5f, 1.5f, 0.0f));
        PortalFrameMesh body;
        PortalFrameMesh reveal;
        buildPortalFrameMesh(glm::vec2(half.x, half.y), look, body, reveal);
        const auto analyse = [&](const PortalFrameMesh& mesh, const char* prefix, Fields& out) {
            const size_t vertexCount = mesh.vertices.size() / 8;
            int nanCount = 0;
            int unitBad = 0;
            for (size_t i = 0; i < vertexCount; ++i) {
                for (size_t k = 0; k < 8; ++k) nanCount += std::isfinite(mesh.vertices[i * 8 + k]) ? 0 : 1;
                const glm::vec3 n(mesh.vertices[i * 8 + 3], mesh.vertices[i * 8 + 4], mesh.vertices[i * 8 + 5]);
                if (std::abs(glm::length(n) - 1.0f) > 1.0e-2f) ++unitBad;
            }
            int badIndex = 0;
            for (unsigned int index : mesh.indices) badIndex += index >= vertexCount ? 1 : 0;
            int degenerate = 0;
            std::map<std::pair<int64_t, int64_t>, int> edges;
            const auto weld = [&](unsigned int index) {
                const int64_t qx = std::llround(mesh.vertices[index * 8] * 10000.0f);
                const int64_t qy = std::llround(mesh.vertices[index * 8 + 1] * 10000.0f);
                const int64_t qz = std::llround(mesh.vertices[index * 8 + 2] * 10000.0f);
                return (qx * 73856093) ^ (qy * 19349663) ^ (qz * 83492791);
            };
            for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
                const unsigned int i0 = mesh.indices[t];
                const unsigned int i1 = mesh.indices[t + 1];
                const unsigned int i2 = mesh.indices[t + 2];
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) continue;
                const glm::vec3 p0(mesh.vertices[i0 * 8], mesh.vertices[i0 * 8 + 1], mesh.vertices[i0 * 8 + 2]);
                const glm::vec3 p1(mesh.vertices[i1 * 8], mesh.vertices[i1 * 8 + 1], mesh.vertices[i1 * 8 + 2]);
                const glm::vec3 p2(mesh.vertices[i2 * 8], mesh.vertices[i2 * 8 + 1], mesh.vertices[i2 * 8 + 2]);
                if (glm::length(glm::cross(p1 - p0, p2 - p0)) < 1.0e-12f) ++degenerate;
                const int64_t w0 = weld(i0);
                const int64_t w1 = weld(i1);
                const int64_t w2 = weld(i2);
                const auto addEdge = [&](int64_t x, int64_t y) { ++edges[{std::min(x, y), std::max(x, y)}]; };
                addEdge(w0, w1);
                addEdge(w1, w2);
                addEdge(w2, w0);
            }
            int boundary = 0;
            int nonManifold = 0;
            for (const auto& [edge, count] : edges) {
                boundary += count == 1 ? 1 : 0;
                nonManifold += count > 2 ? 1 : 0;
            }
            glm::vec3 low(1.0e30f);
            glm::vec3 high(-1.0e30f);
            float minDistance = 1.0e30f;
            for (size_t i = 0; i < vertexCount; ++i) {
                const glm::vec3 position(mesh.vertices[i * 8], mesh.vertices[i * 8 + 1], mesh.vertices[i * 8 + 2]);
                low = glm::min(low, position);
                high = glm::max(high, position);
                minDistance = std::min(minDistance, portalShapeDistance(look.shape, glm::vec2(half.x, half.y), look.cornerRadius, position.x, position.y));
            }
            const std::string key(prefix);
            out.emplace_back(key + "Min", vecV(low));
            out.emplace_back(key + "Max", vecV(high));
            out.emplace_back(key + "MinShapeDistance", numV(minDistance));
            out.emplace_back(key + "Vertices", intV(static_cast<int64_t>(vertexCount)));
            out.emplace_back(key + "Triangles", intV(static_cast<int64_t>(mesh.indices.size() / 3)));
            out.emplace_back(key + "Remainder", intV(static_cast<int64_t>(mesh.indices.size() % 3)));
            out.emplace_back(key + "Nan", intV(nanCount));
            out.emplace_back(key + "BadIndex", intV(badIndex));
            out.emplace_back(key + "NormalBad", intV(unitBad));
            out.emplace_back(key + "Degenerate", intV(degenerate));
            out.emplace_back(key + "Boundary", intV(boundary));
            out.emplace_back(key + "NonManifold", intV(nonManifold));
        };
        Fields out;
        analyse(body, "body", out);
        analyse(reveal, "reveal", out);
        out.emplace_back("key", strV(portalFrameKey(glm::vec2(half.x, half.y), look)));
        return recV(std::move(out));
    });
}

ViewOrientation orientationOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    ViewOrientation o;
    const glm::quat frame = glm::normalize(r.quat("frame", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
    o.frame = glm::mat3_cast(frame);
    o.framed = r.flag("framed", !(std::abs(frame.w) > 0.9999999f));
    if (!o.framed) o.frame = glm::mat3(1.0f);
    o.yaw = r.f("yaw", -90.0f);
    o.pitch = r.f("pitch", 0.0f);
    o.roll = r.f("roll", 0.0f);
    o.relaxing = r.flag("relaxing", false);
    o.rollRate = r.f("rollRate", 0.0f);
    o.pitchRate = r.f("pitchRate", 0.0f);
    return o;
}

Value orientationV(const ViewOrientation& o) {
    glm::vec3 front;
    glm::vec3 up;
    glm::vec3 right;
    orientationBasis(o, front, up, right);
    return recV({{"frame", quatV(glm::quat_cast(o.frame))},
                 {"framed", boolV(o.framed)},
                 {"yaw", numV(o.yaw)},
                 {"pitch", numV(o.pitch)},
                 {"roll", numV(o.roll)},
                 {"relaxing", boolV(o.relaxing)},
                 {"rollRate", numV(o.rollRate)},
                 {"pitchRate", numV(o.pitchRate)},
                 {"front", vecV(front)},
                 {"up", vecV(up)},
                 {"right", vecV(right)},
                 {"frameUp", vecV(o.framed ? o.frame[1] : glm::vec3(0.0f, 1.0f, 0.0f))},
                 {"frameOrtho", numV(orthoErr(glm::mat4(o.frame)))},
                 {"frameDet", numV(glm::dot(o.frame[0], glm::cross(o.frame[1], o.frame[2])))}});
}

void registerCamera(Program& p) {
    reg(p, "xcOrient", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xcOrient");
        return orientationV(orientationOf(a[0], loc));
    });
    reg(p, "xcRotate", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xcRotate");
        ViewOrientation o = orientationOf(a[0], loc);
        rotateOrientation(o, glm::mat3_cast(glm::normalize(quatOf(a[1], loc, "rotation"))));
        return orientationV(o);
    });
    reg(p, "xcRotateMany", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xcRotateMany");
        ViewOrientation o = orientationOf(a[0], loc);
        for (const Value& q : argList(a[1], loc, "rotations")) rotateOrientation(o, glm::mat3_cast(glm::normalize(quatOf(q, loc, "rotation"))));
        return orientationV(o);
    });
    reg(p, "xcCanonical", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xcCanonical");
        ViewOrientation o = orientationOf(a[0], loc);
        canonicalOrientation(o);
        return orientationV(o);
    });
    reg(p, "xcDecompose", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xcDecompose");
        ViewOrientation o = orientationOf(a[0], loc);
        decomposeOrientation(o, glm::normalize(vec3Of(a[1], loc, "front")), glm::normalize(vec3Of(a[2], loc, "up")), static_cast<float>(argNumber(a[3], loc, "yawHint")));
        return orientationV(o);
    });
    reg(p, "xcRelax", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 5, loc, "xcRelax");
        ViewOrientation o = orientationOf(a[0], loc);
        const glm::vec3 target = vec3Of(a[1], loc, "targetUp");
        const float dt = static_cast<float>(argNumber(a[2], loc, "dt"));
        const float seconds = static_cast<float>(argNumber(a[3], loc, "seconds"));
        const int64_t steps = argInt(a[4], loc, "steps");
        ValueList states;
        states.push_back(orientationV(o));
        for (int64_t i = 0; i < steps; ++i) {
            relaxOrientation(o, target, dt, seconds);
            states.push_back(orientationV(o));
        }
        return listV(std::move(states));
    });
    reg(p, "xcCameraCross", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xcCameraCross");
        Camera camera(vec3Of(a[0], loc, "pos"));
        camera.setOrientationState(orientationOf(a[1], loc));
        const glm::mat4 transform = matOf(a[2], loc, "transform");
        const glm::vec3 front = camera.getFront();
        const glm::vec3 up = camera.getUp();
        const glm::vec3 position = camera.getPosition();
        camera.transformBy(transform);
        return recV({{"pos", vecV(camera.getPosition())},
                     {"front", vecV(camera.getFront())},
                     {"up", vecV(camera.getUp())},
                     {"right", vecV(camera.getRight())},
                     {"expectedPos", vecV(glm::vec3(transform * glm::vec4(position, 1.0f)))},
                     {"expectedFront", vecV(glm::normalize(glm::vec3(transform * glm::vec4(front, 0.0f))))},
                     {"expectedUp", vecV(glm::normalize(glm::vec3(transform * glm::vec4(up, 0.0f))))},
                     {"view", matV(camera.getViewMatrix())},
                     {"orientation", orientationV(camera.orientation())}});
    });
    reg(p, "xcSetRotation", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xcSetRotation");
        Camera camera(vec3Of(a[0], loc, "pos"));
        camera.setRotation(glm::normalize(quatOf(a[1], loc, "rotation")));
        const glm::quat out = camera.getRotation();
        return recV({{"rotation", quatV(out)}, {"front", vecV(camera.getFront())}, {"up", vecV(camera.getUp())}, {"pitch", numV(camera.getPitch())}, {"roll", numV(camera.getRoll())}});
    });
}

}

void registerExtPortalBuiltins(Program& program) {
    registerMatrix(program);
    registerPairs(program);
    registerCrossing(program);
    registerFrame(program);
    registerQueries(program);
    registerCells(program);
    registerJsonShapes(program);
    registerCamera(program);
    registerExtPortalStreaming(program);
    registerExtPortalTerrain(program);
    registerExtPortalSave(program);
}

}
