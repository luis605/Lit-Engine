// Bent builtins for terrain height, LOD tile selection, scatter, splat weights and instancing
// (tests/bent/laws/ext_portal_terrain.bent, ext_portal_instancing.bent). Pure functions of their arguments.

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
import Engine.Physics;
import Engine.Portal;
import Engine.Streaming;
import Engine.Terrain;
import Engine.Instancing;
import Engine.mesh;
import Engine.Render.entity;
import Engine.glm;

#include "Engine/Bent/Ext_PortalUtil.hpp"

namespace bent::xp {

namespace {

TerrainConfig terrainOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    NoiseParams noise;
    noise.seed = static_cast<uint64_t>(r.integer("seed", 7));
    noise.amplitude = r.f("amplitude", noise.amplitude);
    noise.frequency = r.f("frequency", noise.frequency);
    noise.octaves = static_cast<int>(r.integer("octaves", noise.octaves));
    TerrainConfig config;
    config.height = makeNoiseHeight(noise);
    config.tileSize = r.f("tileSize", 64.0f);
    config.tileCells = static_cast<int>(r.integer("tileCells", 16));
    config.minHeight = -400.0f;
    config.maxHeight = 400.0f;
    return config;
}

LodParams lodOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    LodParams lod;
    lod.maxLevel = static_cast<int>(r.integer("maxLevel", 3));
    lod.splitFactor = r.f("splitFactor", lod.splitFactor);
    lod.morphStart = r.f("morphStart", lod.morphStart);
    lod.viewDistance = r.f("view", 800.0f);
    lod.balance = r.flag("balance", true);
    return lod;
}

Value tileV(const SelectedTile& tile) {
    return recV({{"x", intV(tile.key.x)}, {"z", intV(tile.key.z)}, {"level", intV(tile.key.level)}, {"distance", numV(tile.distance)}, {"morph", numV(tile.morph)}, {"min", vecV(tile.boundsMin)}, {"max", vecV(tile.boundsMax)}});
}

ScatterConfig scatterOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    ScatterConfig scatter;
    scatter.seed = static_cast<uint64_t>(r.integer("scatterSeed", 31));
    const int64_t species = r.integer("species", 2);
    for (int64_t i = 0; i < species; ++i) {
        ScatterSpecies s;
        s.name = "s" + std::to_string(i);
        s.mesh = static_cast<uint32_t>(i);
        s.spacing = r.f("spacing", 6.0f) + static_cast<float>(i) * 2.0f;
        s.density = r.f("density", 0.8f);
        s.minNormalY = r.f("minNormalY", 0.0f);
        s.clump = r.f("clump", 0.0f);
        s.yOffset = r.f("yOffset", 0.0f);
        scatter.species.push_back(s);
    }
    return scatter;
}

Value instanceV(const ScatterConfig& scatter, const ScatterInstance& i) {
    return recV({{"species", intV(i.species)}, {"pos", vecV(i.position)}, {"normal", vecV(i.normal)}, {"yaw", numV(i.yaw)}, {"scale", numV(i.scale)}, {"cellX", intV(static_cast<int32_t>(i.cellX))}, {"cellZ", intV(static_cast<int32_t>(i.cellZ))}, {"key", strV(scatterInstanceKey(scatter, i))}});
}

void registerTerrain(Program& p) {
    reg(p, "xtHeight", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xtHeight");
        const TerrainConfig config = terrainOf(a[0], loc);
        return numV(terrainHeight(config, static_cast<float>(argNumber(a[1], loc, "x")), static_cast<float>(argNumber(a[2], loc, "z"))));
    });
    reg(p, "xtSelect", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xtSelect");
        const TerrainConfig config = terrainOf(a[0], loc);
        const std::vector<SelectedTile> tiles = selectTerrainTiles(config, vec3Of(a[2], loc, "camera"), lodOf(a[1], loc));
        ValueList list;
        for (const SelectedTile& tile : tiles) list.push_back(tileV(tile));
        return listV(std::move(list));
    });

    // Height samples of a tile: the collider data against the height function at the same positions, edges vs neighbours, and a ray
    // cast onto a HeightfieldCollider built from the samples.
    reg(p, "xtTile", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xtTile");
        const TerrainConfig config = terrainOf(a[0], loc);
        const TileKey key{static_cast<int>(argInt(a[1], loc, "x")), static_cast<int>(argInt(a[2], loc, "z")), static_cast<int>(argInt(a[3], loc, "level"))};
        const std::shared_ptr<HeightfieldData> field = buildTileHeightfield(config, key);
        const glm::vec3 origin = tileOrigin(config, key);
        const float size = tileWorldSize(config, key);
        float sampleErr = 0.0f;
        for (int z = 0; z < field->samplesZ; ++z) {
            for (int x = 0; x < field->samplesX; ++x) {
                const float wx = origin.x + static_cast<float>(x) * field->cellSize;
                const float wz = origin.z + static_cast<float>(z) * field->cellSize;
                sampleErr = std::max(sampleErr, std::abs(field->at(x, z) - terrainHeight(config, wx, wz)));
            }
        }
        // Collider queries at vertices and edge midpoints (field space: the entity origin is the tile corner).
        const HeightfieldData& data = *field;
        float rayVertexErr = 0.0f;
        float rayEdgeErr = 0.0f;
        float heightAtErr = 0.0f;
        int hits = 0;
        int attempts = 0;
        const float top = data.maxHeight + 50.0f;
        const int stride = std::max(1, field->samplesX / 4);
        for (int z = 0; z + 1 < field->samplesZ; z += stride) {
            for (int x = 0; x + 1 < field->samplesX; x += stride) {
                const float lx = static_cast<float>(x) * field->cellSize;
                const float lz = static_cast<float>(z) * field->cellSize;
                ++attempts;
                const std::optional<HeightfieldHit> hit = heightfieldRaycast(data, glm::vec3(lx, top, lz), glm::vec3(0.0f, -1.0f, 0.0f), 4000.0f);
                if (hit) {
                    ++hits;
                    rayVertexErr = std::max(rayVertexErr, std::abs((top - hit->distance) - field->at(x, z)));
                }
                heightAtErr = std::max(heightAtErr, std::abs(heightfieldHeightAt(data, lx, lz) - field->at(x, z)));
                // Mid point of the x edge between (x, z) and (x + 1, z): on the grid line, so linear.
                const float mx = lx + 0.5f * field->cellSize;
                const std::optional<HeightfieldHit> mid = heightfieldRaycast(data, glm::vec3(mx, top, lz), glm::vec3(0.0f, -1.0f, 0.0f), 4000.0f);
                if (mid) rayEdgeErr = std::max(rayEdgeErr, std::abs((top - mid->distance) - 0.5f * (field->at(x, z) + field->at(x + 1, z))));
                heightAtErr = std::max(heightAtErr, std::abs(heightfieldHeightAt(data, mx, lz) - 0.5f * (field->at(x, z) + field->at(x + 1, z))));
            }
        }
        ValueList edges;
        // Left / right / near / far edge rows as lists of heights.
        const auto edge = [&](int fixedAxis, int index) {
            ValueList row;
            for (int i = 0; i < (fixedAxis == 0 ? field->samplesZ : field->samplesX); ++i) row.push_back(numV(fixedAxis == 0 ? field->at(index, i) : field->at(i, index)));
            return listV(std::move(row));
        };
        return recV({{"samples", intV(field->samplesX)},
                     {"cell", numV(field->cellSize)},
                     {"size", numV(size)},
                     {"origin", vecV(origin)},
                     {"sampleErr", numV(sampleErr)},
                     {"rayVertexErr", numV(rayVertexErr)},
                     {"rayEdgeErr", numV(rayEdgeErr)},
                     {"heightAtErr", numV(heightAtErr)},
                     {"hits", intV(hits)},
                     {"attempts", intV(attempts)},
                     {"minHeight", numV(field->minHeight)},
                     {"maxHeight", numV(field->maxHeight)},
                     {"left", edge(0, 0)},
                     {"right", edge(0, field->samplesX - 1)},
                     {"near", edge(1, 0)},
                     {"far", edge(1, field->samplesZ - 1)}});
    });

    reg(p, "xtScatter", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xtScatter");
        const TerrainConfig config = terrainOf(a[0], loc);
        const ScatterConfig scatter = scatterOf(a[0], loc);
        const TileKey key{static_cast<int>(argInt(a[1], loc, "x")), static_cast<int>(argInt(a[2], loc, "z")), static_cast<int>(argInt(a[3], loc, "level"))};
        const std::vector<ScatterInstance> instances = scatterTile(config, scatter, key);
        ValueList list;
        for (const ScatterInstance& i : instances) {
            Value v = instanceV(scatter, i);
            list.push_back(v);
        }
        return recV({{"instances", listV(std::move(list))}, {"min", vecV(tileOrigin(config, key))}, {"size", numV(tileWorldSize(config, key))}});
    });
    reg(p, "xtScatterGround", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 4, loc, "xtScatterGround");
        const TerrainConfig config = terrainOf(a[0], loc);
        const ScatterConfig scatter = scatterOf(a[0], loc);
        const TileKey key{static_cast<int>(argInt(a[1], loc, "x")), static_cast<int>(argInt(a[2], loc, "z")), static_cast<int>(argInt(a[3], loc, "level"))};
        float worst = 0.0f;
        for (const ScatterInstance& i : scatterTile(config, scatter, key)) {
            const float ground = terrainHeight(config, i.position.x, i.position.z) + scatter.species[i.species].yOffset;
            worst = std::max(worst, std::abs(i.position.y - ground));
        }
        return numV(worst);
    });
    reg(p, "xtSplat", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xtSplat");
        SplatRules rules;
        const glm::vec4 w = splatWeights(static_cast<float>(argNumber(a[0], loc, "height")), static_cast<float>(argNumber(a[1], loc, "slope")), rules);
        const TerrainLayer dominant = dominantLayer(w);
        (void)a[2];
        return recV({{"grass", numV(w.x)}, {"rock", numV(w.y)}, {"sand", numV(w.z)}, {"snow", numV(w.w)}, {"dominant", intV(static_cast<int>(dominant))}});
    });

    // Terrain streamer in deterministic mode along a camera path: resident / visible tile sets per step.
    reg(p, "xtStream", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xtStream");
        TerrainLoaderConfig loader;
        loader.terrain = terrainOf(a[0], loc);
        auto uploads = std::make_shared<std::atomic<int>>(0);
        loader.uploadMesh = [uploads](const Mesh&) { return static_cast<uint32_t>(++*uploads); };
        const Rec s = rec(a[1], loc);
        TerrainStreamConfig stream;
        stream.lod = lodOf(a[1], loc);
        stream.deterministic = true;
        stream.reselectInterval = 1;
        stream.evictDelay = static_cast<uint32_t>(s.integer("evictDelay", 3));
        stream.maxResidentTiles = static_cast<uint32_t>(s.integer("maxResident", 1024));
        stream.workers = static_cast<uint32_t>(s.integer("workers", 2));
        World world;
        world.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
        TerrainStreamer streamer(world, std::move(loader), stream);
        ValueList steps;
        const bool prime = s.flag("prime", true);
        bool first = true;
        for (const Value& position : argList(a[2], loc, "path")) {
            const glm::vec3 camera = vec3Of(position, loc, "camera");
            const glm::vec3 forward(0.0f, 0.0f, -1.0f);
            if (first && prime) streamer.prime(camera, forward, nullptr);
            else streamer.update(camera, forward, nullptr);
            first = false;
            ValueList visible;
            for (const TileKey& key : streamer.visibleTiles()) visible.push_back(recV({{"x", intV(key.x)}, {"z", intV(key.z)}, {"level", intV(key.level)}}));
            ValueList resident;
            for (const TileKey& key : streamer.residentTiles()) resident.push_back(recV({{"x", intV(key.x)}, {"z", intV(key.z)}, {"level", intV(key.level)}}));
            ValueList wanted;
            for (const SelectedTile& tile : streamer.wantedTiles()) wanted.push_back(recV({{"x", intV(tile.key.x)}, {"z", intV(tile.key.z)}, {"level", intV(tile.key.level)}}));
            steps.push_back(recV({{"visible", listV(std::move(visible))}, {"resident", listV(std::move(resident))}, {"wanted", listV(std::move(wanted))}, {"settled", boolV(streamer.settled())}, {"gpuMeshes", intV(streamer.stats().gpuMeshes)}}));
        }
        return listV(std::move(steps));
    });
}

// ---------------------------------------------------------------- instancing

InstanceData instanceOf(const Value& value, const SrcLoc& loc) {
    const Rec r = rec(value, loc);
    InstanceData d;
    d.position = r.vec("pos", glm::vec3(0.0f));
    d.scale = r.f("scale", 1.0f);
    d.rotation = glm::normalize(r.quat("rot", glm::quat(1.0f, 0.0f, 0.0f, 0.0f)));
    d.color = static_cast<uint32_t>(r.integer("color", 0xFFFFFFFFll));
    d.seed = static_cast<uint32_t>(r.integer("seed", 0)) & 0xFFFFFFu;
    d.flags = static_cast<uint32_t>(r.integer("flags", 0));
    return d;
}

Value instanceDataV(const InstanceData& d) {
    return recV({{"pos", vecV(d.position)}, {"scale", numV(d.scale)}, {"rot", quatV(d.rotation)}, {"color", intV(d.color)}, {"seed", intV(d.seed)}, {"flags", intV(d.flags)}});
}

Value batchV(const InstanceBatch& batch) {
    ValueList instances;
    for (const InstanceData& d : batch.instances) instances.push_back(instanceDataV(d));
    ValueList keys;
    for (uint64_t k : batch.keys) keys.push_back(intV(static_cast<int64_t>(k)));
    return recV({{"instances", listV(std::move(instances))}, {"keys", listV(std::move(keys))}, {"version", intV(static_cast<int64_t>(batch.version))}, {"layoutVersion", intV(static_cast<int64_t>(batch.layoutVersion))}, {"dirtyFirst", intV(batch.dirtyFirst)}, {"dirtyEnd", intV(batch.dirtyEnd)}, {"dirty", boolV(batch.dirty())}, {"alive", intV(batch.aliveCount())}});
}

void registerInstancing(Program& p) {
    // Apply operations {op, ...} to an InstanceBatch (keys kept when given) and report the final batch and the per-op results.
    reg(p, "xiBatch", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xiBatch");
        InstanceBatch batch;
        std::vector<InstanceData> initial;
        for (const Value& item : argList(a[0], loc, "initial")) initial.push_back(instanceOf(item, loc));
        batch.assign(initial);
        for (const Value& key : argList(a[1], loc, "keys")) batch.keys.push_back(static_cast<uint64_t>(argInt(key, loc, "key")));
        batch.clearDirty();
        ValueList results;
        ValueList snapshots;
        for (const Value& item : argList(a[2], loc, "ops")) {
            const Rec op = rec(item, loc);
            const std::string name = op.str("op", "");
            const uint64_t version = batch.version;
            const uint64_t layout = batch.layoutVersion;
            Value result = Value::unit();
            if (name == "update") {
                std::vector<InstanceData> data;
                for (const Value& d : op.list("data")) data.push_back(instanceOf(d, loc));
                batch.update(static_cast<uint32_t>(op.integer("first", 0)), data);
            } else if (name == "remove") {
                result = boolV(batch.remove(static_cast<uint32_t>(op.integer("index", 0))));
            } else if (name == "revive") {
                result = boolV(batch.revive(static_cast<uint32_t>(op.integer("index", 0))));
            } else if (name == "swapRemove") {
                const std::optional<uint64_t> moved = batch.swapRemove(static_cast<uint32_t>(op.integer("index", 0)));
                result = moved ? intV(static_cast<int64_t>(*moved)) : Value::unit();
            } else if (name == "find") {
                const std::optional<uint32_t> index = batch.indexOfKey(static_cast<uint64_t>(op.integer("key", 0)));
                result = index ? intV(*index) : intV(-1);
            } else if (name == "clearDirty") {
                batch.clearDirty();
            }
            snapshots.push_back(recV({{"op", strV(name)}, {"result", result}, {"versionBumped", boolV(batch.version != version)}, {"layoutBumped", boolV(batch.layoutVersion != layout)}, {"count", intV(static_cast<int64_t>(batch.instances.size()))}, {"keys", intV(static_cast<int64_t>(batch.keys.size()))}, {"dirtyFirst", intV(batch.dirtyFirst)}, {"dirtyEnd", intV(batch.dirtyEnd)}}));
        }
        return recV({{"batch", batchV(batch)}, {"steps", listV(std::move(snapshots))}});
    });

    reg(p, "xiPack", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xiPack");
        const InstanceData in = instanceOf(a[0], loc);
        const InstanceData out = unpackInstance(packInstance(in));
        const float dot = std::abs(in.rotation.w * out.rotation.w + in.rotation.x * out.rotation.x + in.rotation.y * out.rotation.y + in.rotation.z * out.rotation.z);
        return recV({{"posErr", numV(glm::length(in.position - out.position))}, {"scaleErr", numV(std::abs(in.scale - out.scale))}, {"rotDot", numV(dot)}, {"colorSame", boolV(in.color == out.color)}, {"seedSame", boolV(in.seed == out.seed)}, {"flagsSame", boolV(in.flags == out.flags)}, {"out", instanceDataV(out)}});
    });
    reg(p, "xiHash01", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xiHash01");
        return numV(instanceHash01(static_cast<uint32_t>(argInt(a[0], loc, "seed"))));
    });
    reg(p, "xiLod", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 1, loc, "xiLod");
        return intV(instanceLodForDiameter(static_cast<float>(argNumber(a[0], loc, "diameter"))));
    });

    // Allocator model: ops {op: "alloc", cells} / {op: "free", slot} / {op: "clear"}; reports results and state after every op.
    reg(p, "xiPool", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xiPool");
        InstanceAllocator pool(static_cast<uint32_t>(argInt(a[0], loc, "limit")));
        std::vector<std::pair<uint32_t, uint32_t>> live;  // first, cells of every successful allocation (index = allocation number)
        std::vector<bool> freed;
        ValueList steps;
        for (const Value& item : argList(a[1], loc, "ops")) {
            const Rec op = rec(item, loc);
            const std::string name = op.str("op", "");
            Value result = Value::unit();
            if (name == "alloc") {
                const uint32_t cells = static_cast<uint32_t>(op.integer("cells", 1));
                const std::optional<uint32_t> first = pool.allocate(cells);
                if (first) {
                    live.emplace_back(*first, cells);
                    freed.push_back(false);
                    result = intV(*first);
                }
            } else if (name == "free") {
                if (!live.empty()) {
                    // The n-th still-live allocation, modulo the live count.
                    std::vector<size_t> alive;
                    for (size_t i = 0; i < live.size(); ++i) {
                        if (!freed[i]) alive.push_back(i);
                    }
                    if (!alive.empty()) {
                        const size_t chosen = alive[static_cast<size_t>(op.integer("slot", 0)) % alive.size()];
                        pool.free(live[chosen].first, live[chosen].second);
                        freed[chosen] = true;
                        result = intV(static_cast<int64_t>(chosen));
                    }
                }
            } else if (name == "clear") {
                pool.clear();
                std::fill(freed.begin(), freed.end(), true);
            }
            steps.push_back(recV({{"op", strV(name)}, {"result", result}, {"used", intV(pool.usedCells())}, {"high", intV(pool.highWater())}, {"ranges", intV(pool.freeRanges())}}));
        }
        ValueList liveList;
        for (size_t i = 0; i < live.size(); ++i) liveList.push_back(recV({{"first", intV(live[i].first)}, {"cells", intV(live[i].second)}, {"freed", boolV(freed[i])}}));
        return recV({{"steps", listV(std::move(steps))}, {"allocations", listV(std::move(liveList))}});
    });

    // Spatial cells of a batch: the order is a permutation, chunks of 64 form cells, every cell sphere contains its live instances.
    reg(p, "xiCellBounds", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 3, loc, "xiCellBounds");
        std::vector<InstanceData> instances;
        for (const Value& item : argList(a[0], loc, "instances")) instances.push_back(instanceOf(item, loc));
        const glm::vec3 center = vec3Of(a[1], loc, "meshCenter");
        const float radius = static_cast<float>(argNumber(a[2], loc, "meshRadius"));
        const std::vector<uint32_t> order = instanceSpatialOrder(instances);
        ValueList orderList;
        for (uint32_t index : order) orderList.push_back(intV(index));
        ValueList cells;
        const glm::vec4 meshSphere(center, radius);
        for (size_t first = 0; first < order.size(); first += kInstanceCellSize) {
            const size_t count = std::min<size_t>(kInstanceCellSize, order.size() - first);
            const std::span<const uint32_t> slots(order.data() + first, count);
            const InstanceCellGpu cell = buildInstanceCell(instances, slots, meshSphere, 3);
            // Worst containment violation: instance mesh sphere extends beyond the cell sphere.
            float worst = -1.0e30f;
            uint32_t live = 0;
            for (uint32_t slot : slots) {
                const InstanceData& d = instances[slot];
                if ((d.flags & InstanceFlags::Dead) != 0) continue;
                ++live;
                const glm::vec3 c = d.position + d.rotation * (center * d.scale);
                const float extent = glm::length(c - glm::vec3(cell.sphere)) + radius * d.scale - cell.sphere.w;
                worst = std::max(worst, extent);
            }
            ValueList members;
            for (uint32_t slot : slots) members.push_back(intV(slot));
            cells.push_back(recV({{"count", intV(cell.info[1])}, {"live", intV(cell.info[2])}, {"expectedLive", intV(live)}, {"violation", numV(live == 0 ? 0.0 : worst)}, {"radius", numV(cell.sphere.w)}, {"members", listV(std::move(members))}}));
        }
        return recV({{"order", listV(std::move(orderList))}, {"cells", listV(std::move(cells))}});
    });

    // CPU reference cull of a batch from a perspective camera.
    reg(p, "xiCull", [](const ValueList& a, const SrcLoc& loc) {
        checkArity(a, 2, loc, "xiCull");
        const Rec b = rec(a[0], loc);
        InstanceBatch batch;
        std::vector<InstanceData> instances;
        for (const Value& item : b.list("instances")) instances.push_back(instanceOf(item, loc));
        batch.assign(instances);
        batch.settings.drawDistance = b.f("drawDistance", 0.0f);
        batch.settings.fadeStart = b.f("fadeStart", 0.0f);
        batch.settings.lodBias = b.f("lodBias", 1.0f);
        const glm::vec4 meshSphere(b.vec("meshCenter", glm::vec3(0.0f)), b.f("meshRadius", 1.0f));
        const Rec v = rec(a[1], loc);
        const glm::vec3 position = v.vec("pos", glm::vec3(0.0f));
        const glm::vec3 forward = glm::normalize(v.vec("forward", glm::vec3(0.0f, 0.0f, -1.0f)));
        const glm::mat4 view = glm::lookAt(position, position + forward, std::abs(forward.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(v.f("fov", 60.0f)), v.f("aspect", 1.5f), v.f("far", 500.0f), v.f("near", 0.1f));
        InstanceCullView cull;
        cull.planes = instanceFrustumPlanes(projection * view);
        cull.view = view;
        cull.position = position;
        cull.pixelScale = v.f("pixelScale", projection[1][1] * 1080.0f);
        cull.lodBias = v.f("lodBias", 1.0f);
        cull.distanceScale = v.f("distanceScale", 1.0f);
        cull.forcedLod = static_cast<int>(v.integer("forcedLod", -1));
        cull.cellCull = v.flag("cellCull", true);
        const InstanceCullResult result = cullInstancesReference(batch, glm::mat4(1.0f), meshSphere, cull);
        ValueList lods;
        uint32_t lodSum = 0;
        for (uint32_t n : result.perLod) {
            lods.push_back(intV(n));
            lodSum += n;
        }
        return recV({{"visible", intV(result.visible)}, {"cellsTested", intV(result.cellsTested)}, {"cellsAccepted", intV(result.cellsAccepted)}, {"instancesTested", intV(result.instancesTested)}, {"perLod", listV(std::move(lods))}, {"lodSum", intV(lodSum)}});
    });
}

}

void registerExtPortalTerrain(Program& program) {
    registerTerrain(program);
    registerInstancing(program);
}

}
