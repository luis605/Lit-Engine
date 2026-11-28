#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Engine/Bent/EngineHost.hpp"
#include "Engine/Render/TextureSlots.hpp"

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
import Engine.WorldHash;
import Engine.Replay;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Json;
import Engine.SceneValidator;
import Engine.Render.settings;
import Engine.Render.governor;
import Engine.Render.material;
import Engine.Render.lighting;
import Engine.Render.graph;
import Engine.meshlet;
import Engine.Params;
import Engine.camera;
import Engine.glm;

#include "Engine/Bent/EngineSim.hpp"

namespace bent {

namespace {

const Value* fld(const Value& record, const char* name) { return record.kind == Kind::Record ? record.field(name) : nullptr; }

double getNum(const Value& record, const char* name, double fallback) {
    const Value* v = fld(record, name);
    return v && v->isNumber() ? v->num() : fallback;
}

int64_t getInt(const Value& record, const char* name, int64_t fallback) {
    const Value* v = fld(record, name);
    if (!v || !v->isNumber()) return fallback;
    const double d = v->num();
    if (!std::isfinite(d)) return fallback;
    return static_cast<int64_t>(std::clamp(d, -9.0e15, 9.0e15));
}

bool getBool(const Value& record, const char* name, bool fallback) {
    const Value* v = fld(record, name);
    return v && v->kind == Kind::Bool ? v->b : fallback;
}

glm::vec3 getVec(const Value& record, const char* name, const glm::vec3& fallback) {
    const Value* v = fld(record, name);
    return v && v->kind == Kind::Vec3 ? glm::vec3(static_cast<float>(v->v[0]), static_cast<float>(v->v[1]), static_cast<float>(v->v[2])) : fallback;
}

Value num(double d) { return Value::number(d); }
Value integer(int64_t i) { return Value::integer(i); }
Value boolean(bool b) { return Value::boolean(b); }
Value str(std::string s) { return Value::string(std::move(s)); }

std::string settingText(const Value& value) {
    if (value.kind == Kind::Str) return value.text();
    if (value.kind == Kind::Bool) return value.b ? "1" : "0";
    return print(value);
}

std::string dumpSettings(const RenderSettings& settings) {
    std::ostringstream out;
    writeRenderSettings(settings, out, "");
    return out.str();
}

std::vector<std::string> settingsViolations(const RenderSettings& s) {
    std::vector<std::string> bad;
    RenderSettings copy = s;
    visitRenderSettings(copy, [&](std::string_view name, auto& field) {
        using T = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<T, float>) {
            if (!std::isfinite(field)) bad.push_back(std::string(name) + " is not finite");
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            if (!std::isfinite(field.x) || !std::isfinite(field.y) || !std::isfinite(field.z)) bad.push_back(std::string(name) + " is not finite");
        }
    });
    const auto check = [&](bool ok, const char* what) {
        if (!ok) bad.push_back(what);
    };
    check(static_cast<int>(s.sky) >= 0 && static_cast<int>(s.sky) <= 2, "sky out of range");
    check(static_cast<int>(s.tonemapper) >= 0 && static_cast<int>(s.tonemapper) <= 5, "tonemapper out of range");
    check(static_cast<int>(s.debugView) >= 0 && static_cast<int>(s.debugView) <= 11, "debugView out of range");
    check(static_cast<int>(s.reflectionMode) >= 0 && static_cast<int>(s.reflectionMode) < kReflectionModeCount, "reflectionMode out of range");
    check(static_cast<int>(s.reflectionQuality) >= 0 && static_cast<int>(s.reflectionQuality) < kReflectionQualityCount, "reflectionQuality out of range");
    check(s.renderScale >= kRenderScaleMin && s.renderScale <= kRenderScaleMax, "renderScale out of range");
    check(s.targetFrameMs >= kTargetFrameMsMin && s.targetFrameMs <= kTargetFrameMsMax, "targetFrameMs out of range");
    check(s.upscaleSharpness >= 0.0f && s.upscaleSharpness <= 1.0f, "upscaleSharpness out of range");
    check(s.governorMinScale >= kRenderScaleMin && s.governorMinScale <= kRenderScaleMax, "governorMinScale out of range");
    check(s.aoDenoise >= 0 && s.aoDenoise <= kAoDenoiseMax, "aoDenoise out of range");
    check(s.vsmPoolSize == 2048 || s.vsmPoolSize == 4096 || s.vsmPoolSize == 8192, "vsmPoolSize not 2048/4096/8192");
    check(s.vsmMaxMip >= 0 && s.vsmMaxMip <= 4, "vsmMaxMip out of range");
    check(s.reflectionIntensity >= 0.0f && s.reflectionIntensity <= 4.0f, "reflectionIntensity out of range");
    check(s.reflectionTraceDistance >= 1.0f && s.reflectionTraceDistance <= 1000.0f, "reflectionTraceDistance out of range");
    check(s.shadowCascadeBlend >= 0.0f && s.shadowCascadeBlend <= 0.5f, "shadowCascadeBlend out of range");
    check(s.vsmResolutionScale >= 0.25f && s.vsmResolutionScale <= 8.0f, "vsmResolutionScale out of range");
    return bad;
}

Value stringList(const std::vector<std::string>& items) {
    ValueList out;
    for (const std::string& item : items) out.push_back(str(item));
    return Value::makeList(std::move(out));
}

struct Soup {
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
};

Soup makeSphere(int subdivisions, float bump, float freq, glm::vec3 scale) {
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<std::array<float, 3>> points = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0}, {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t}, {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    std::vector<uint32_t> faces = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11, 1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8, 3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9, 4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    for (auto& p : points) {
        const float length = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
        for (float& c : p) c /= length;
    }
    for (int level = 0; level < subdivisions; ++level) {
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            const auto key = std::minmax(a, b);
            if (auto it = midpoints.find(key); it != midpoints.end()) return it->second;
            std::array<float, 3> m{};
            for (int axis = 0; axis < 3; ++axis) m[axis] = points[a][axis] + points[b][axis];
            const float length = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
            for (float& c : m) c /= length;
            points.push_back(m);
            return midpoints[key] = static_cast<uint32_t>(points.size() - 1);
        };
        std::vector<uint32_t> next;
        for (size_t f = 0; f < faces.size(); f += 3) {
            const uint32_t a = faces[f], b = faces[f + 1], c = faces[f + 2];
            const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
            next.insert(next.end(), {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca});
        }
        faces = std::move(next);
    }
    Soup soup;
    for (const auto& p : points) {
        const float wobble = 1.0f + bump * std::sin(freq * p[0]) * std::cos(0.8f * freq * p[1]) + 0.4f * bump * std::sin(3.4f * freq * p[2]);
        soup.vertices.insert(soup.vertices.end(), {p[0] * wobble * scale.x, p[1] * wobble * scale.y, p[2] * wobble * scale.z, p[0], p[1], p[2], 0.5f + p[0] * 0.5f, 0.5f + p[1] * 0.5f});
    }
    soup.indices = std::move(faces);
    return soup;
}

std::array<uint32_t, 3> clusterTriangle(const ClusterMesh& mesh, const MeshCluster& cluster, uint32_t index) {
    const uint32_t packed = mesh.triangles[cluster.triangleOffset + index];
    return {mesh.vertices[cluster.vertexOffset + (packed & 0xFFu)], mesh.vertices[cluster.vertexOffset + ((packed >> 8) & 0xFFu)], mesh.vertices[cluster.vertexOffset + ((packed >> 16) & 0xFFu)]};
}

float dist3(const float* a, const float* b) { return std::sqrt((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2])); }

size_t openEdges(const ClusterMesh& mesh, const std::vector<uint32_t>& cut) {
    std::map<std::pair<uint32_t, uint32_t>, int> edges;
    for (const uint32_t id : cut) {
        const MeshCluster& cluster = mesh.clusters[id];
        for (uint32_t t = 0; t < cluster.triangleCount; ++t) {
            const auto tri = clusterTriangle(mesh, cluster, t);
            for (int e = 0; e < 3; ++e) ++edges[std::minmax(tri[e], tri[(e + 1) % 3])];
        }
    }
    size_t open = 0;
    for (const auto& [edge, count] : edges) open += count % 2 != 0 ? 1 : 0;
    return open;
}

struct BuiltMesh {
    Soup soup;
    ClusterMesh mesh;
    ClusterHierarchy hierarchy;
    uint64_t hash = 0;
};

uint64_t fnv(uint64_t h, const void* data, size_t bytes) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < bytes; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

std::shared_ptr<BuiltMesh> buildMesh(const Value& spec) {
    static std::mutex lock;
    static std::map<std::string, std::shared_ptr<BuiltMesh>> cache;
    const bool nocache = getBool(spec, "nocache", false);
    const std::string key = print(spec);
    if (!nocache) {
        std::lock_guard<std::mutex> guard(lock);
        if (const auto it = cache.find(key); it != cache.end()) return it->second;
    }
    auto built = std::make_shared<BuiltMesh>();
    built->soup = makeSphere(static_cast<int>(std::clamp<int64_t>(getInt(spec, "subdiv", 3), 0, 4)), static_cast<float>(getNum(spec, "bump", 0.08)), static_cast<float>(getNum(spec, "freq", 5.0)), getVec(spec, "scale", glm::vec3(1.0f)));
    ClusterBuildOptions options;
    options.groupSize = static_cast<uint32_t>(std::clamp<int64_t>(getInt(spec, "groupSize", 4), 2, 8));
    options.minReduction = static_cast<float>(getNum(spec, "minReduction", 0.85));
    built->mesh = buildClusterMesh(built->soup.vertices, 8, built->soup.indices, options);
    ClusterHierarchyOptions hopts;
    hopts.leafSize = static_cast<uint32_t>(std::clamp<int64_t>(getInt(spec, "leafSize", 64), 1, 64));
    hopts.fanout = static_cast<uint32_t>(std::clamp<int64_t>(getInt(spec, "fanout", 8), 2, 64));
    hopts.rootFanout = static_cast<uint32_t>(std::clamp<int64_t>(getInt(spec, "rootFanout", 64), 2, 64));
    built->hierarchy = buildClusterHierarchy(built->mesh, hopts);
    uint64_t h = 14695981039346656037ull;
    h = fnv(h, built->mesh.clusters.data(), built->mesh.clusters.size() * sizeof(MeshCluster));
    h = fnv(h, built->mesh.vertices.data(), built->mesh.vertices.size() * sizeof(uint32_t));
    h = fnv(h, built->mesh.triangles.data(), built->mesh.triangles.size() * sizeof(uint32_t));
    h = fnv(h, built->hierarchy.nodes.data(), built->hierarchy.nodes.size() * sizeof(ClusterNode));
    built->hash = h;
    if (!nocache) {
        std::lock_guard<std::mutex> guard(lock);
        if (cache.size() > 64) cache.clear();
        cache[key] = built;
    }
    return built;
}

Value meshReport(const BuiltMesh& built) {
    const ClusterMesh& mesh = built.mesh;
    std::string error;
    const bool valid = validateClusterMesh(mesh, built.soup.vertices.size() / 8, &error);
    std::string hierError;
    const bool hierValid = validateClusterHierarchy(mesh, built.hierarchy, &hierError);
    int64_t monotoneViolations = 0;
    int64_t levelViolations = 0;
    int64_t roots = 0;
    int64_t oversize = 0;
    double boundsViolation = -1e30;
    double selfViolation = -1e30;
    for (const MeshCluster& cluster : mesh.clusters) {
        if (!(cluster.parentError >= cluster.selfError)) ++monotoneViolations;
        if (cluster.parentError == kClusterRootError) ++roots;
        if (cluster.vertexCount > kClusterMaxVertices || cluster.triangleCount > kClusterMaxTriangles) ++oversize;
        for (uint32_t v = 0; v < cluster.vertexCount; ++v) {
            const float* p = &built.soup.vertices[mesh.vertices[cluster.vertexOffset + v] * 8];
            boundsViolation = std::max<double>(boundsViolation, dist3(p, cluster.boundsCenter) - cluster.boundsRadius);
            selfViolation = std::max<double>(selfViolation, dist3(p, cluster.selfCenter) - cluster.selfRadius);
        }
    }
    for (const ClusterLevel& level : mesh.levels) {
        for (uint32_t id = level.firstCluster; id < level.firstCluster + level.clusterCount && id < mesh.clusters.size(); ++id) {
            if (mesh.clusters[id].selfError < level.minSelfError || mesh.clusters[id].parentError > level.maxParentError) ++levelViolations;
        }
    }
    ValueList levelTris;
    for (uint32_t level = 0; level < mesh.levels.size(); ++level) levelTris.push_back(integer(static_cast<int64_t>(mesh.triangleCount(level))));
    return Value::record({{"clusters", integer(static_cast<int64_t>(mesh.clusters.size()))},
                          {"levels", integer(static_cast<int64_t>(mesh.levels.size()))},
                          {"levelTris", Value::makeList(std::move(levelTris))},
                          {"inputTris", integer(static_cast<int64_t>(built.soup.indices.size() / 3))},
                          {"valid", boolean(valid)},
                          {"validError", str(error)},
                          {"hierValid", boolean(hierValid)},
                          {"hierError", str(hierError)},
                          {"hierNodes", integer(static_cast<int64_t>(built.hierarchy.nodes.size()))},
                          {"hierDepth", integer(built.hierarchy.depth)},
                          {"hierLeaves", integer(built.hierarchy.leaves)},
                          {"monotoneViolations", integer(monotoneViolations)},
                          {"levelViolations", integer(levelViolations)},
                          {"roots", integer(roots)},
                          {"oversize", integer(oversize)},
                          {"boundsViolation", num(boundsViolation)},
                          {"selfViolation", num(selfViolation)},
                          {"hash", integer(static_cast<int64_t>(built.hash & 0x7fffffffffffffffULL))}});
}

struct FakeBackend final : rg::Backend {
    uintptr_t next = 0x1000;
    int created = 0;
    int destroyed = 0;
    int transitions = 0;
    void* createTexture(const rg::TextureDesc&, std::string_view) override {
        ++created;
        next += 0x10;
        return reinterpret_cast<void*>(next);
    }
    void* createBuffer(const rg::BufferDesc&, std::string_view) override {
        ++created;
        next += 0x10;
        return reinterpret_cast<void*>(next);
    }
    void destroy(void*, rg::ResourceType) override { ++destroyed; }
    void transition(std::span<const rg::Barrier> list) override { transitions += static_cast<int>(list.size()); }
    void beginTiming(int) override {}
    void endTiming(int) override {}
};

Value runGraph(const Value& spec, const SrcLoc& loc) {
    const Value* transients = fld(spec, "transients");
    const Value* passes = fld(spec, "passes");
    if (!transients || transients->kind != Kind::List || !passes || passes->kind != Kind::List) throw BentError(loc, "rgRun: spec needs transients and passes lists");
    const size_t transientCount = transients->items().size();
    const size_t importCount = static_cast<size_t>(std::clamp<int64_t>(getInt(spec, "imports", 1), 0, 16));
    const int frames = static_cast<int>(std::clamp<int64_t>(getInt(spec, "frames", 1), 1, 6));
    rg::TransientPool pool(static_cast<uint32_t>(std::clamp<int64_t>(getInt(spec, "retire", 8), 1, 64)));
    FakeBackend backend;
    std::vector<int> importTargets(importCount + 1);

    ValueList frameResults;
    for (int frame = 0; frame < frames; ++frame) {
        rg::RenderGraph graph;
        std::vector<rg::Handle> handles;
        for (size_t i = 0; i < importCount; ++i) handles.push_back(graph.importTexture("i" + std::to_string(i), &importTargets[i]));
        for (size_t i = 0; i < transientCount; ++i) {
            rg::TextureDesc desc;
            const int64_t id = static_cast<int64_t>(transients->items()[i].num());
            desc.width = 16u * static_cast<uint32_t>(1 + (id % 3));
            desc.height = 16u;
            desc.format = static_cast<uint32_t>(id / 3 % 2);
            handles.push_back(graph.createTexture("t" + std::to_string(i), desc));
        }
        std::vector<int64_t> executed;
        uint32_t index = 0;
        for (const Value& pass : passes->items()) {
            std::vector<rg::Handle> reads;
            std::vector<rg::Handle> writes;
            const auto collect = [&](const char* key, std::vector<rg::Handle>& out) {
                if (const Value* list = fld(pass, key); list && list->kind == Kind::List) {
                    for (const Value& r : list->items()) {
                        if (!r.isNumber()) continue;
                        const auto id = static_cast<int64_t>(r.num());
                        if (id >= 0 && static_cast<size_t>(id) < handles.size()) out.push_back(handles[static_cast<size_t>(id)]);
                    }
                }
            };
            collect("reads", reads);
            collect("writes", writes);
            const bool side = getBool(pass, "side", false);
            const int64_t passId = index;
            graph.addPass("p" + std::to_string(index), [&reads, &writes, side](rg::PassBuilder& b) {
                for (const rg::Handle h : reads) b.read(h, rg::Usage::ShaderResource);
                for (const rg::Handle h : writes) b.write(h, rg::Usage::RenderTarget);
                if (side) b.sideEffect();
            }, [&executed, passId](rg::PassContext&) { executed.push_back(passId); });
            ++index;
        }
        graph.compile();
        ValueList culled;
        for (uint32_t p = 0; p < graph.passCount(); ++p) culled.push_back(boolean(graph.culled(p)));
        ValueList order;
        for (const uint32_t p : graph.order()) order.push_back(integer(p));
        graph.execute(backend, pool);
        ValueList physical;
        for (size_t i = 0; i < transientCount; ++i) physical.push_back(integer(graph.physicalOf(handles[importCount + i])));
        ValueList ran;
        for (const int64_t p : executed) ran.push_back(integer(p));
        const rg::GraphStats& stats = graph.stats();
        frameResults.push_back(Value::record({{"order", Value::makeList(std::move(order))},
                                              {"ran", Value::makeList(std::move(ran))},
                                              {"culled", Value::makeList(std::move(culled))},
                                              {"physical", Value::makeList(std::move(physical))},
                                              {"culledPasses", integer(stats.culledPasses)},
                                              {"allocatedTransients", integer(stats.allocatedTransients)},
                                              {"physicalTransients", integer(stats.physicalTransients)},
                                              {"transientBytes", integer(static_cast<int64_t>(stats.transientBytes))},
                                              {"physicalBytes", integer(static_cast<int64_t>(stats.physicalBytes))},
                                              {"created", integer(backend.created)},
                                              {"destroyed", integer(backend.destroyed)},
                                              {"poolSize", integer(pool.size())}}));
        pool.endFrame(backend);
    }
    pool.clear(backend);
    return Value::record({{"frames", Value::makeList(std::move(frameResults))}, {"createdTotal", integer(backend.created)}, {"destroyedTotal", integer(backend.destroyed)}, {"poolAfterClear", integer(pool.size())}});
}

Camera makeCamera(const Value& spec) {
    const float nearPlane = static_cast<float>(getNum(spec, "near", 0.1));
    const float farPlane = static_cast<float>(getNum(spec, "far", 100.0));
    Camera camera(getVec(spec, "pos", glm::vec3(0.0f)), glm::vec3(0.0f, 1.0f, 0.0f), -90.0f, 0.0f, nearPlane, farPlane);
    camera.setOrientation(static_cast<float>(getNum(spec, "yaw", -90.0)), static_cast<float>(getNum(spec, "pitch", 0.0)), static_cast<float>(getNum(spec, "roll", 0.0)));
    camera.setFov(static_cast<float>(getNum(spec, "fov", 60.0)));
    camera.updateAspectRatio(static_cast<float>(getNum(spec, "w", 16.0)), static_cast<float>(getNum(spec, "h", 9.0)));
    camera.setNearPlane(nearPlane);
    camera.setFarPlane(farPlane);
    if (getBool(spec, "ortho", false)) {
        camera.setOrthographic(true);
        camera.setOrthoHeight(static_cast<float>(getNum(spec, "orthoHeight", 10.0)));
    }
    return camera;
}

double det3(const glm::mat4& m) {
    return static_cast<double>(m[0][0]) * (m[1][1] * m[2][2] - m[2][1] * m[1][2]) - static_cast<double>(m[1][0]) * (m[0][1] * m[2][2] - m[2][1] * m[0][2]) + static_cast<double>(m[2][0]) * (m[0][1] * m[1][2] - m[1][1] * m[0][2]);
}

Value v4(const glm::vec4& v) { return Value::record({{"x", num(v.x)}, {"y", num(v.y)}, {"z", num(v.z)}, {"w", num(v.w)}}); }

bool lineKey(const std::string& line, std::string& key, std::string& rest) {
    const size_t space = line.find(' ');
    if (space == std::string::npos) return false;
    key = line.substr(0, space);
    rest = line.substr(space + 1);
    return true;
}

}

namespace ext {

void registerExtRenderBuiltins(Program& program) {
    program.native("rxRsApply", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "rsApply(pairs)");
        RenderSettings settings;
        ValueList accepted;
        for (const Value& pair : argList(args[0], loc, "rsApply")) {
            if (pair.kind != Kind::List || pair.items().size() != 2) throw BentError(loc, "rsApply: pairs are [key, value]");
            accepted.push_back(boolean(setRenderSetting(settings, argString(pair.items()[0], loc, "key"), settingText(pair.items()[1]))));
        }
        return Value::record({{"dump", str(dumpSettings(settings))}, {"accepted", Value::makeList(std::move(accepted))}, {"violations", stringList(settingsViolations(settings))}});
    });
    program.native("rxRsLoadDump", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "rsLoadDump(text)");
        RenderSettings settings;
        std::istringstream in(argString(args[0], loc, "rsLoadDump"));
        std::string line, key, rest;
        int64_t rejected = 0;
        while (std::getline(in, line)) {
            if (!lineKey(line, key, rest) || !setRenderSetting(settings, key, rest)) ++rejected;
        }
        return Value::record({{"dump", str(dumpSettings(settings))}, {"rejected", integer(rejected)}, {"violations", stringList(settingsViolations(settings))}});
    });
    program.native("rxRsNames", [](Interp&, const ValueList&, const NamedArgs&, const SrcLoc&) {
        RenderSettings settings;
        ValueList names;
        visitRenderSettings(settings, [&](std::string_view name, auto&) { names.push_back(str(std::string(name))); });
        return Value::makeList(std::move(names));
    });
    program.native("rxDumpField", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "rxDumpField(dump, key)");
        std::istringstream in(argString(args[0], loc, "dump"));
        const std::string prefix = argString(args[1], loc, "key") + " ";
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, prefix.size(), prefix) == 0) return str(line.substr(prefix.size()));
        }
        return Value::unit();
    });
    program.native("rxDumpNum", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "rxDumpNum(dump, key)");
        std::istringstream in(argString(args[0], loc, "dump"));
        const std::string prefix = argString(args[1], loc, "key") + " ";
        std::string line;
        while (std::getline(in, line)) {
            if (line.compare(0, prefix.size(), prefix) == 0) return num(std::strtod(line.c_str() + prefix.size(), nullptr));
        }
        throw BentError(loc, "rxDumpNum: no such key " + prefix);
    });
    program.native("rxRsDefaultDump", [](Interp&, const ValueList&, const NamedArgs&, const SrcLoc&) { return str(dumpSettings(RenderSettings{})); });
    program.native("rxRsViolationsOfDefault", [](Interp&, const ValueList&, const NamedArgs&, const SrcLoc&) { return stringList(settingsViolations(RenderSettings{})); });
    program.native("rxRsKind", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "rsKind(key)");
        const std::string wanted = argString(args[0], loc, "rsKind");
        std::string kind;
        RenderSettings settings;
        visitRenderSettings(settings, [&](std::string_view name, auto& field) {
            using T = std::decay_t<decltype(field)>;
            if (name != wanted) return;
            if constexpr (std::is_same_v<T, float>) kind = "float";
            else if constexpr (std::is_same_v<T, bool>) kind = "bool";
            else if constexpr (std::is_same_v<T, int>) kind = "int";
            else if constexpr (std::is_same_v<T, std::string>) kind = "string";
            else if constexpr (std::is_same_v<T, glm::vec3>) kind = "vec3";
            else kind = "enum";
        });
        return str(kind);
    });

    program.native("rxGovRun", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "govRun(config, samples)");
        RenderSettings settings;
        settings.renderScale = static_cast<float>(getNum(args[0], "scale", 1.0));
        settings.shadowResolution = static_cast<int>(getInt(args[0], "shadow", 2048));
        settings.reflectionQuality = static_cast<ReflectionQuality>(std::clamp<int64_t>(getInt(args[0], "refl", 2), 0, 2));
        settings.governorMinScale = static_cast<float>(getNum(args[0], "minScale", 0.6));
        settings.targetFrameMs = static_cast<float>(getNum(args[0], "target", 16.6));
        settings.qualityGovernor = getBool(args[0], "enabled", true);
        clampQualitySettings(settings);
        const GovernorLimits baseline = currentLimits(settings);
        QualityGovernor governor;
        ValueList levels, scales, shadows, refls, changed;
        for (const Value& sample : argList(args[1], loc, "govRun")) {
            if (sample.kind != Kind::List || sample.items().size() != 2) throw BentError(loc, "govRun: samples are [frameMs, dt]");
            const bool did = updateGovernor(governor, settings, static_cast<float>(sample.items()[0].num()), static_cast<float>(sample.items()[1].num()));
            levels.push_back(integer(governor.level));
            scales.push_back(num(settings.renderScale));
            shadows.push_back(integer(settings.shadowResolution));
            refls.push_back(integer(static_cast<int>(settings.reflectionQuality)));
            changed.push_back(boolean(did));
        }
        return Value::record({{"levels", Value::makeList(std::move(levels))},
                              {"scales", Value::makeList(std::move(scales))},
                              {"shadows", Value::makeList(std::move(shadows))},
                              {"refls", Value::makeList(std::move(refls))},
                              {"changed", Value::makeList(std::move(changed))},
                              {"baseScale", num(baseline.renderScale)},
                              {"baseShadow", integer(baseline.shadowResolution)},
                              {"baseRefl", integer(baseline.reflectionQuality)},
                              {"minScale", num(settings.governorMinScale)},
                              {"target", num(settings.targetFrameMs)}});
    });
    program.native("rxGovLimitsAt", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 5) throw BentError(loc, "govLimitsAt(level, scale, shadow, minScale, refl)");
        GovernorLimits base;
        base.renderScale = quantizeRenderScale(static_cast<float>(argNumber(args[1], loc, "scale")));
        base.shadowResolution = static_cast<int>(argInt(args[2], loc, "shadow"));
        base.reflectionQuality = static_cast<int>(argInt(args[4], loc, "refl"));
        const GovernorLimits limits = governorLimitsAt(static_cast<int>(argInt(args[0], loc, "level")), base, static_cast<float>(argNumber(args[3], loc, "minScale")));
        return Value::record({{"scale", num(limits.renderScale)}, {"shadow", integer(limits.shadowResolution)}, {"refl", integer(limits.reflectionQuality)}});
    });

    program.native("rxCamProject", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "camProject(spec)");
        const Camera camera = makeCamera(args[0]);
        const glm::vec3 point = getVec(args[0], "point", glm::vec3(0.0f));
        const glm::mat4 view = camera.getViewMatrix();
        const glm::mat4 proj = camera.getProjectionMatrix();
        const glm::vec4 clip = proj * view * glm::vec4(point, 1.0f);
        const glm::vec4 viewSpace = view * glm::vec4(point, 1.0f);
        const glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return Value::record({{"clip", v4(clip)},
                              {"ndc", Value::vec3(ndc.x, ndc.y, ndc.z)},
                              {"viewDepth", num(-viewSpace.z)},
                              {"viewX", num(viewSpace.x)},
                              {"viewY", num(viewSpace.y)},
                              {"front", Value::vec3(camera.getFront().x, camera.getFront().y, camera.getFront().z)},
                              {"right", Value::vec3(camera.getRight().x, camera.getRight().y, camera.getRight().z)},
                              {"up", Value::vec3(camera.getUp().x, camera.getUp().y, camera.getUp().z)},
                              {"viewDet", num(det3(view))}});
    });

    program.native("rxMatPack", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "matPack(material)");
        Material m;
        const Value& r = args[0];
        m.baseColor = getVec(r, "baseColor", m.baseColor);
        m.opacity = static_cast<float>(getNum(r, "opacity", m.opacity));
        m.metallic = static_cast<float>(getNum(r, "metallic", m.metallic));
        m.roughness = static_cast<float>(getNum(r, "roughness", m.roughness));
        m.reflectance = static_cast<float>(getNum(r, "reflectance", m.reflectance));
        m.occlusion = static_cast<float>(getNum(r, "occlusion", m.occlusion));
        m.emissive = getVec(r, "emissive", m.emissive);
        m.emissiveIntensity = static_cast<float>(getNum(r, "emissiveIntensity", m.emissiveIntensity));
        m.clearcoat = static_cast<float>(getNum(r, "clearcoat", m.clearcoat));
        m.clearcoatRoughness = static_cast<float>(getNum(r, "clearcoatRoughness", m.clearcoatRoughness));
        m.anisotropy = static_cast<float>(getNum(r, "anisotropy", m.anisotropy));
        m.subsurface = static_cast<float>(getNum(r, "subsurface", m.subsurface));
        m.transmission = static_cast<float>(getNum(r, "transmission", m.transmission));
        m.ior = static_cast<float>(getNum(r, "ior", m.ior));
        m.thickness = static_cast<float>(getNum(r, "thickness", m.thickness));
        m.uvScale = static_cast<float>(getNum(r, "uvScale", m.uvScale));
        m.normalStrength = static_cast<float>(getNum(r, "normalStrength", m.normalStrength));
        m.parallaxDepth = static_cast<float>(getNum(r, "parallaxDepth", m.parallaxDepth));
        m.triplanar = getBool(r, "triplanar", false);
        m.baseColorTexture = static_cast<int>(getInt(r, "baseColorTexture", NO_TEXTURE));
        m.normalTexture = static_cast<int>(getInt(r, "normalTexture", NO_TEXTURE));
        m.ormTexture = static_cast<int>(getInt(r, "ormTexture", NO_TEXTURE));
        m.emissiveTexture = static_cast<int>(getInt(r, "emissiveTexture", NO_TEXTURE));
        const MaterialGpu p = packMaterial(m);
        const auto floats = [&](const glm::vec4& v) { return Value::makeList({num(v.x), num(v.y), num(v.z), num(v.w)}); };
        return Value::record({{"baseColorOpacity", floats(p.baseColorOpacity)},
                              {"emissive", floats(p.emissive)},
                              {"pbr", floats(p.pbr)},
                              {"coat", floats(p.coat)},
                              {"sheen", floats(p.sheen)},
                              {"subsurface", floats(p.subsurface)},
                              {"transmission", floats(p.transmission)},
                              {"textures", Value::makeList({integer(p.textures[0]), integer(p.textures[1]), integer(p.textures[2]), integer(p.textures[3])})},
                              {"flags", floats(p.flags)}});
    });

    program.native("rxClusterReport", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "clusterReport(spec)");
        return meshReport(*buildMesh(args[0]));
    });
    program.native("rxClusterCuts", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "clusterCuts(spec, queries)");
        const auto built = buildMesh(args[0]);
        ValueList out;
        for (const Value& q : argList(args[1], loc, "clusterCuts")) {
            ClusterLodView view;
            const glm::vec3 cam = getVec(q, "cam", glm::vec3(0.0f, 0.0f, 5.0f));
            view.origin[0] = cam.x;
            view.origin[1] = cam.y;
            view.origin[2] = cam.z;
            view.errorScale = static_cast<float>(getNum(q, "scale", 800.0));
            view.nearPlane = static_cast<float>(getNum(q, "near", 0.01));
            view.orthographic = getBool(q, "ortho", false);
            const float threshold = static_cast<float>(getNum(q, "threshold", 1.0));
            std::vector<uint32_t> flat = selectClusterCut(built->mesh, view, threshold);
            ClusterTraversalStats stats;
            std::vector<uint32_t> hier = selectClusterCutHierarchy(built->mesh, built->hierarchy, view, threshold, &stats);
            std::sort(flat.begin(), flat.end());
            std::sort(hier.begin(), hier.end());
            int64_t triangles = 0;
            int64_t minLevel = 1 << 20;
            int64_t maxLevel = -1;
            bool allRoots = !flat.empty();
            for (const uint32_t id : flat) {
                const MeshCluster& c = built->mesh.clusters[id];
                triangles += c.triangleCount;
                minLevel = std::min<int64_t>(minLevel, c.level);
                maxLevel = std::max<int64_t>(maxLevel, c.level);
                allRoots = allRoots && c.parentError == kClusterRootError;
            }
            ValueList flatIds, hierIds;
            for (const uint32_t id : flat) flatIds.push_back(integer(id));
            for (const uint32_t id : hier) hierIds.push_back(integer(id));
            out.push_back(Value::record({{"flat", Value::makeList(std::move(flatIds))},
                                         {"hier", Value::makeList(std::move(hierIds))},
                                         {"open", integer(static_cast<int64_t>(openEdges(built->mesh, flat)))},
                                         {"tris", integer(triangles)},
                                         {"minLevel", integer(minLevel)},
                                         {"maxLevel", integer(maxLevel)},
                                         {"allRoots", boolean(allRoots)},
                                         {"nodesVisited", integer(static_cast<int64_t>(stats.nodesVisited))},
                                         {"clustersTested", integer(static_cast<int64_t>(stats.clustersTested))}}));
        }
        return Value::makeList(std::move(out));
    });
    program.native("rxClusterLevelZero", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "clusterLevelZero(spec)");
        const auto built = buildMesh(args[0]);
        ValueList ids;
        for (uint32_t id = 0; id < built->mesh.clusters.size(); ++id) {
            if (built->mesh.clusters[id].level == 0) ids.push_back(integer(id));
        }
        std::sort(ids.begin(), ids.end(), [](const Value& a, const Value& b) { return a.i < b.i; });
        return Value::makeList(std::move(ids));
    });
    program.native("rxClusterProjected", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 6) throw BentError(loc, "clusterProjected(center, radius, error, camera, scale, near)");
        const glm::vec3 c = toVec(args[0], loc, "center");
        const glm::vec3 cam = toVec(args[3], loc, "camera");
        const float center[3] = {c.x, c.y, c.z};
        const float camera[3] = {cam.x, cam.y, cam.z};
        return num(clusterProjectedError(center, static_cast<float>(argNumber(args[1], loc, "radius")), static_cast<float>(argNumber(args[2], loc, "error")), camera, static_cast<float>(argNumber(args[4], loc, "scale")), static_cast<float>(argNumber(args[5], loc, "near"))));
    });
    program.native("rxClusterFuzz", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "clusterFuzz(points, indices)");
        std::vector<float> vertices;
        for (const Value& p : argList(args[0], loc, "points")) {
            const glm::vec3 v = toVec(p, loc, "point");
            vertices.insert(vertices.end(), {v.x, v.y, v.z, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f});
        }
        std::vector<uint32_t> indices;
        const size_t count = vertices.size() / 8;
        for (const Value& i : argList(args[1], loc, "indices")) indices.push_back(static_cast<uint32_t>(count ? static_cast<uint64_t>(argInt(i, loc, "index")) % count : 0));
        indices.resize(indices.size() / 3 * 3);
        ClusterMesh mesh = buildClusterMesh(vertices, 8, indices);
        std::string error;
        const bool valid = mesh.empty() || validateClusterMesh(mesh, count, &error);
        std::string hierError;
        bool hierValid = true;
        if (!mesh.empty()) {
            const ClusterHierarchy hierarchy = buildClusterHierarchy(mesh);
            hierValid = validateClusterHierarchy(mesh, hierarchy, &hierError);
        }
        return Value::record({{"empty", boolean(mesh.empty())}, {"valid", boolean(valid)}, {"error", str(error)}, {"hierValid", boolean(hierValid)}, {"hierError", str(hierError)},
                              {"level0", integer(mesh.empty() ? 0 : static_cast<int64_t>(mesh.triangleCount(0)))}, {"inputTris", integer(static_cast<int64_t>(indices.size() / 3))}});
    });

    program.native("rxRgRun", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "rgRun(spec)");
        return runGraph(args[0], loc);
    });

    program.native("rxSlotsRun", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "slotsRun(capacity, ops)");
        texslots::SlotAllocator allocator(static_cast<uint32_t>(std::clamp<int64_t>(argInt(args[0], loc, "capacity"), 0, 4096)));
        ValueList results, used, highWater;
        for (const Value& op : argList(args[1], loc, "slotsRun")) {
            const int64_t code = argInt(op, loc, "op");
            if (code < 0) results.push_back(integer(allocator.allocate()));
            else results.push_back(integer(allocator.release(static_cast<int>(std::min<int64_t>(code, 1 << 30))) ? 1 : 0));
            used.push_back(integer(allocator.used()));
            highWater.push_back(integer(allocator.highWater()));
        }
        ValueList allocated;
        for (uint32_t slot = 0; slot < allocator.highWater(); ++slot) {
            if (allocator.allocated(static_cast<int>(slot))) allocated.push_back(integer(slot));
        }
        return Value::record({{"results", Value::makeList(std::move(results))}, {"used", Value::makeList(std::move(used))}, {"highWater", Value::makeList(std::move(highWater))},
                              {"allocated", Value::makeList(std::move(allocated))}, {"full", boolean(allocator.full())}});
    });

    program.native("rxParamsLoad", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "paramsLoad(text)");
        ParamBus bus;
        std::string error;
        const bool ok = bus.loadText(argString(args[0], loc, "paramsLoad"), &error);
        ValueList params;
        for (const ParamInfo& p : bus.params()) {
            params.push_back(Value::record({{"name", str(p.name)}, {"slot", integer(p.slot)}, {"value", num(p.value)}, {"target", num(p.target)}, {"min", num(p.minimum)}, {"max", num(p.maximum)}, {"derived", boolean(p.derived)}}));
        }
        return Value::record({{"ok", boolean(ok)}, {"error", str(error)}, {"params", Value::makeList(std::move(params))}});
    });
    program.native("rxParamsDrive", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 2) throw BentError(loc, "paramsDrive(text, ops)");
        ParamBus bus;
        std::string error;
        if (!bus.loadText(argString(args[0], loc, "paramsDrive"), &error)) throw BentError(loc, "paramsDrive: text does not load: " + error);
        ValueList values;
        for (const Value& op : argList(args[1], loc, "paramsDrive")) {
            const ValueList& o = op.items();
            if (o.size() != 3) throw BentError(loc, "paramsDrive: ops are [name, value, seconds]");
            const std::string name = argString(o[0], loc, "name");
            bus.set(name, static_cast<float>(o[1].num()));
            const int steps = static_cast<int>(std::clamp(o[2].num() * 60.0, 0.0, 600.0));
            for (int i = 0; i < steps; ++i) bus.update(1.0f / 60.0f);
            std::vector<std::pair<std::string, Value>> all;
            for (const ParamInfo& p : bus.params()) all.emplace_back(p.name, num(p.value));
            values.push_back(Value::record(std::move(all)));
        }
        return Value::makeList(std::move(values));
    });
    program.native("rxCurveEval", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 3) throw BentError(loc, "curveEval(points, mode, x)");
        ParamCurve curve;
        for (const Value& p : argList(args[0], loc, "points")) curve.points.emplace_back(static_cast<float>(p.items()[0].num()), static_cast<float>(p.items()[1].num()));
        const std::string mode = argString(args[1], loc, "mode");
        curve.mode = mode == "smooth" ? CurveMode::Smooth : (mode == "step" ? CurveMode::Step : CurveMode::Linear);
        return num(curve.eval(static_cast<float>(argNumber(args[2], loc, "x"))));
    });

    program.native("rxSceneSaveText", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "sceneSaveText(program)");
        EngineSim sim;
        for (const ActionV* action : programActions(args[0], loc)) sim.apply(*action, loc, [] { return true; });
        std::stringstream text;
        if (!sim.world().saveScene(text)) throw BentError(loc, "saveScene failed");
        return str(text.str());
    });
    program.native("rxSceneLoadText", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "sceneLoadText(text)");
        EngineSim sim;
        std::istringstream in(argString(args[0], loc, "sceneLoadText"));
        const bool ok = sim.world().loadScene(in);
        int64_t entities = 0;
        sim.world().forEach([&](EntityHandle) { ++entities; });
        const std::vector<std::string> errors = sim.world().validate();
        const std::vector<ValidationIssue> issues = validateScene(sim.world());
        return Value::record({{"ok", boolean(ok)}, {"entities", integer(entities)}, {"errors", stringList(errors)}, {"validatorErrors", integer(static_cast<int64_t>(countIssues(issues, Severity::Error)))}});
    });
    program.native("rxValidatorCodes", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "validatorCodes(program)");
        EngineSim sim;
        for (const ActionV* action : programActions(args[0], loc)) sim.apply(*action, loc, [] { return true; });
        std::vector<std::string> codes;
        for (const ValidationIssue& issue : validateScene(sim.world())) codes.push_back(std::string(severityName(issue.severity)) + ":" + issue.code);
        std::sort(codes.begin(), codes.end());
        return stringList(codes);
    });
    program.native("rxExplainFrame", [](Interp&, const ValueList& args, const NamedArgs&, const SrcLoc& loc) {
        if (args.size() != 1) throw BentError(loc, "explainFrame(facts)");
        const Value& r = args[0];
        FrameFacts facts;
        facts.cpuMs = getNum(r, "cpuMs", 0.0);
        facts.gpuMs = getNum(r, "gpuMs", 0.0);
        facts.targetMs = getNum(r, "targetMs", 16.6);
        facts.scene.drawables = static_cast<size_t>(std::max<int64_t>(0, getInt(r, "drawables", 0)));
        facts.scene.shadowCasters = static_cast<size_t>(std::max<int64_t>(0, getInt(r, "casters", 0)));
        facts.scene.lights = static_cast<size_t>(std::max<int64_t>(0, getInt(r, "lights", 0)));
        facts.scene.transparent = static_cast<size_t>(std::max<int64_t>(0, getInt(r, "transparent", 0)));
        facts.shadowResolution = static_cast<int>(getInt(r, "shadowResolution", 2048));
        facts.shadowCascades = static_cast<int>(getInt(r, "shadowCascades", 4));
        if (const Value* passes = fld(r, "passes"); passes && passes->kind == Kind::List) {
            for (const Value& p : passes->items()) {
                if (p.kind == Kind::List && p.items().size() == 2 && p.items()[0].kind == Kind::Str) facts.gpuPasses.emplace_back(p.items()[0].text(), p.items()[1].num());
            }
        }
        ValueList out;
        for (const FrameFinding& f : explainFrame(facts)) out.push_back(Value::record({{"weight", num(f.weight)}, {"title", str(f.title)}, {"advice", str(f.advice)}}));
        return Value::makeList(std::move(out));
    });
}

}

}
