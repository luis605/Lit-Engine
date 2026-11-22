#include "TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

import Engine.World;
import Engine.Render.entity;
import Engine.Portal;
import Engine.Physics;
import Engine.Character;
import Engine.Streaming;
import Engine.Terrain;
import Engine.mesh;
import Engine.Rng;
import Engine.glm;

static void setup(World& w) {
    w.setMeshBoundsHook([](uint32_t) { return glm::vec4(0.0f, 0.0f, 0.0f, 1.0f); });
}

static TerrainConfig makeConfig(uint64_t seed = 7) {
    NoiseParams noise;
    noise.seed = seed;
    TerrainConfig config;
    config.height = makeNoiseHeight(noise);
    config.tileSize = 64.0f;
    config.tileCells = 32;
    config.minHeight = -200.0f;
    config.maxHeight = 200.0f;
    return config;
}

static std::shared_ptr<HeightfieldData> rampField(int samples, float cell, float slopeX) {
    auto field = std::make_shared<HeightfieldData>();
    field->samplesX = samples;
    field->samplesZ = samples;
    field->cellSize = cell;
    field->heights.resize(static_cast<size_t>(samples) * static_cast<size_t>(samples));
    for (int z = 0; z < samples; ++z) {
        for (int x = 0; x < samples; ++x) field->heights[static_cast<size_t>(z * samples + x)] = slopeX * static_cast<float>(x) * cell;
    }
    field->updateBounds();
    return field;
}

static EntityHandle addField(World& w, const std::shared_ptr<const HeightfieldData>& data, const glm::vec3& at) {
    EntityDesc d;
    d.position = at;
    const EntityHandle e = w.create(d);
    HeightfieldCollider collider;
    collider.data = data;
    w.add<HeightfieldCollider>(e, collider);
    return e;
}

static void testNoise() {
    const TerrainConfig a = makeConfig(7);
    const TerrainConfig b = makeConfig(7);
    const TerrainConfig c = makeConfig(8);
    float minimum = 1e9f;
    float maximum = -1e9f;
    bool same = true;
    bool differs = false;
    for (int i = 0; i < 400; ++i) {
        const float x = static_cast<float>(i) * 13.7f - 2000.0f;
        const float z = static_cast<float>(i % 17) * 91.3f - 700.0f;
        const float h = a.height(x, z);
        same = same && h == b.height(x, z);
        differs = differs || h != c.height(x, z);
        minimum = std::min(minimum, h);
        maximum = std::max(maximum, h);
        CHECK(std::isfinite(h));
    }
    CHECK(same);
    CHECK(differs);
    CHECK(maximum - minimum > 20.0f);
    CHECK(maximum < 400.0f && minimum > -400.0f);

    // Tiles share edges exactly.
    const auto left = buildTileHeightfield(a, TileKey{0, 0, 0});
    const auto right = buildTileHeightfield(a, TileKey{1, 0, 0});
    bool seamless = true;
    for (int z = 0; z < left->samplesZ; ++z) seamless = seamless && left->at(left->samplesX - 1, z) == right->at(0, z);
    CHECK(seamless);
    CHECK(left->maxHeight >= left->minHeight);
}

static void testHeightmap() {
    std::vector<uint8_t> raw;
    for (int z = 0; z < 4; ++z) {
        for (int x = 0; x < 4; ++x) {
            const uint16_t v = static_cast<uint16_t>(x * 20000);
            raw.push_back(static_cast<uint8_t>(v & 0xFF));
            raw.push_back(static_cast<uint8_t>(v >> 8));
        }
    }
    const auto image = HeightImage::fromRaw16(raw, 4, 4);
    CHECK(image.has_value());
    CHECK(image && image->samples[1] == 20000);
    CHECK(!HeightImage::fromRaw16(raw, 8, 8).has_value());
    const auto big = HeightImage::fromRaw16(raw, 4, 4, true);
    CHECK(big && big->samples[1] != 20000);

    std::string pgm = "P5\n# comment\n3 2\n65535\n";
    std::vector<uint8_t> bytes(pgm.begin(), pgm.end());
    for (uint16_t v : {0, 32768, 65535, 0, 32768, 65535}) {
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v & 0xFF));
    }
    const auto parsed = HeightImage::fromPgm(bytes);
    CHECK(parsed && parsed->width == 3 && parsed->depth == 2 && parsed->samples[2] == 65535);
    CHECK(!HeightImage::fromPgm({'P', '6'}).has_value());

    HeightImagePlacement placement;
    placement.cellSize = 10.0f;
    placement.minHeight = 0.0f;
    placement.maxHeight = 100.0f;
    const HeightFn fn = makeImageHeight(std::make_shared<HeightImage>(*parsed), placement);
    CHECK(std::abs(fn(0.0f, 0.0f)) < 0.01f);
    CHECK(std::abs(fn(20.0f, 0.0f) - 100.0f) < 0.01f);
    CHECK(std::abs(fn(10.0f, 5.0f) - 50.0f) < 0.1f);
    CHECK(std::abs(fn(500.0f, 5.0f) - 100.0f) < 0.01f);
}

static float coveredArea(const std::vector<SelectedTile>& tiles) {
    float area = 0.0f;
    for (const SelectedTile& t : tiles) area += (t.boundsMax.x - t.boundsMin.x) * (t.boundsMax.z - t.boundsMin.z);
    return area;
}

static void testLod() {
    TerrainConfig config = makeConfig();
    LodParams lod;
    lod.maxLevel = 4;
    lod.viewDistance = 1500.0f;
    const glm::vec3 camera(100.0f, 30.0f, -40.0f);
    const auto tiles = selectTerrainTiles(config, camera, lod);
    CHECK(!tiles.empty());
    CHECK(tiles.size() < 400);

    int nearestLevel = 99;
    float nearestDistance = 1e9f;
    int farLevel = -1;
    float farDistance = -1.0f;
    bool overlap = false;
    bool morphOk = true;
    std::set<std::tuple<int, int, int>> seen;
    for (const SelectedTile& t : tiles) {
        morphOk = morphOk && t.morph >= 0.0f && t.morph <= 1.0f;
        if (t.distance < nearestDistance) {
            nearestDistance = t.distance;
            nearestLevel = t.key.level;
        }
        if (t.distance > farDistance) {
            farDistance = t.distance;
            farLevel = t.key.level;
        }
        overlap = overlap || !seen.insert({t.key.x, t.key.z, t.key.level}).second;
        const float size = tileWorldSize(config, t.key);
        CHECK(std::abs(t.boundsMin.x - static_cast<float>(t.key.x) * size) < 1e-3f);
    }
    CHECK(morphOk);
    CHECK(!overlap);
    CHECK(nearestLevel == 0);
    CHECK(farLevel >= 3);
    // No two selected tiles overlap in the plane: pairwise check on a subset grid.
    bool disjoint = true;
    for (size_t i = 0; i < tiles.size() && disjoint; ++i) {
        for (size_t j = i + 1; j < tiles.size(); ++j) {
            const auto& a = tiles[i];
            const auto& b = tiles[j];
            if (a.boundsMin.x < b.boundsMax.x - 1e-3f && b.boundsMin.x < a.boundsMax.x - 1e-3f && a.boundsMin.z < b.boundsMax.z - 1e-3f && b.boundsMin.z < a.boundsMax.z - 1e-3f) {
                disjoint = false;
                break;
            }
        }
    }
    CHECK(disjoint);
    // Every point near the camera is covered by a tile.
    bool covered = true;
    for (int i = -10; i <= 10; ++i) {
        for (int j = -10; j <= 10; ++j) {
            const float x = camera.x + static_cast<float>(i) * 37.0f;
            const float z = camera.z + static_cast<float>(j) * 37.0f;
            bool hit = false;
            for (const SelectedTile& t : tiles) hit = hit || (x >= t.boundsMin.x && x < t.boundsMax.x && z >= t.boundsMin.z && z < t.boundsMax.z);
            covered = covered && hit;
        }
    }
    CHECK(covered);
    CHECK(coveredArea(tiles) > 0.0f);

    // Moving the camera far away changes the selection; levels are monotone with distance along a ray.
    const auto moved = selectTerrainTiles(config, camera + glm::vec3(5000.0f, 0.0f, 0.0f), lod);
    CHECK(!moved.empty());
    int previousLevel = -1;
    bool monotone = true;
    for (int step = 0; step < 12; ++step) {
        const float x = camera.x + static_cast<float>(step) * 120.0f;
        for (const SelectedTile& t : tiles) {
            if (x >= t.boundsMin.x && x < t.boundsMax.x && camera.z >= t.boundsMin.z && camera.z < t.boundsMax.z) {
                if (previousLevel > t.key.level + 0) monotone = false;
                previousLevel = std::max(previousLevel, t.key.level);
            }
        }
    }
    CHECK(monotone);
    // A leaf's morph reaches 1 near the range where its parent would take over.
    LodParams fine = lod;
    fine.maxLevel = 0;
    for (const SelectedTile& t : selectTerrainTiles(config, camera, fine)) CHECK(t.key.level == 0 && t.morph == 0.0f);
}

static void testMesh() {
    const TerrainConfig config = makeConfig();
    const TileKey key{2, -1, 0};
    const TerrainTileMesh tile = buildTileMesh(config, key);
    const int m = config.tileCells;
    const size_t surface = static_cast<size_t>((m + 1) * (m + 1));
    CHECK(tile.surfaceVertices == surface);
    CHECK(tile.mesh.vertices.size() == (surface + 4 * static_cast<size_t>(m + 1)) * kVertexFloats);
    CHECK(tile.splat.size() == tile.mesh.vertices.size() / kVertexFloats);
    CHECK(tile.morph.size() == tile.splat.size());
    CHECK(tile.mesh.indices.size() == static_cast<size_t>(m * m) * 6 + static_cast<size_t>(m) * 4 * 6);

    const size_t vertexCount = tile.mesh.vertices.size() / kVertexFloats;
    const auto position = [&](unsigned int i) { return glm::vec3(tile.mesh.vertices[i * kVertexFloats], tile.mesh.vertices[i * kVertexFloats + 1], tile.mesh.vertices[i * kVertexFloats + 2]); };
    bool inRange = true;
    bool upFacing = true;
    bool skirtOut = true;
    size_t surfaceTriangles = static_cast<size_t>(m * m) * 2;
    for (size_t t = 0; t < tile.mesh.indices.size() / 3; ++t) {
        const unsigned int a = tile.mesh.indices[t * 3];
        const unsigned int b = tile.mesh.indices[t * 3 + 1];
        const unsigned int c = tile.mesh.indices[t * 3 + 2];
        inRange = inRange && a < vertexCount && b < vertexCount && c < vertexCount;
        const glm::vec3 normal = glm::cross(position(b) - position(a), position(c) - position(a));
        if (t < surfaceTriangles) {
            upFacing = upFacing && normal.y > 0.0f;
        } else {
            const glm::vec3 center = (position(a) + position(b) + position(c)) / 3.0f - glm::vec3(32.0f, 0.0f, 32.0f);
            const float outward = glm::dot(glm::vec3(normal.x, 0.0f, normal.z), center);
            skirtOut = skirtOut && outward > 0.0f;
        }
    }
    CHECK(inRange);
    CHECK(upFacing);
    CHECK(skirtOut);

    bool unitNormals = true;
    bool weightsOk = true;
    for (size_t v = 0; v < vertexCount; ++v) {
        const glm::vec3 n(tile.mesh.vertices[v * kVertexFloats + 3], tile.mesh.vertices[v * kVertexFloats + 4], tile.mesh.vertices[v * kVertexFloats + 5]);
        unitNormals = unitNormals && std::abs(glm::length(n) - 1.0f) < 1e-3f;
        const glm::vec4 s = tile.splat[v];
        weightsOk = weightsOk && std::abs(s.x + s.y + s.z + s.w - 1.0f) < 1e-3f && s.x >= 0.0f && s.y >= 0.0f && s.z >= 0.0f && s.w >= 0.0f;
    }
    CHECK(unitNormals);
    CHECK(weightsOk);
    // Skirt is below the surface and bounds include it.
    CHECK(tile.boundsMin.y < position(0).y);
    // Morph is zero on the coarse lattice.
    CHECK(tile.morph[0] == 0.0f && tile.morph[2] == 0.0f);

    // Vertex heights match the heightfield and the terrain function.
    const glm::vec3 origin = tileOrigin(config, key);
    CHECK(std::abs(position(5).y - terrainHeight(config, origin.x + position(5).x, origin.z + position(5).z)) < 1e-4f);

    // Coarser tiles keep the same vertex count; stride reduces it; flipped winding inverts facing.
    TileMeshOptions coarse;
    coarse.stride = 2;
    coarse.skirts = false;
    const TerrainTileMesh lowDetail = buildTileMesh(config, key, coarse);
    CHECK(lowDetail.mesh.vertices.size() == static_cast<size_t>(17 * 17) * kVertexFloats);
    TileMeshOptions flipped;
    flipped.skirts = false;
    flipped.flipWinding = true;
    const TerrainTileMesh back = buildTileMesh(config, key, flipped);
    {
        const unsigned int a = back.mesh.indices[0];
        const unsigned int b = back.mesh.indices[1];
        const unsigned int c = back.mesh.indices[2];
        const glm::vec3 pa(back.mesh.vertices[a * kVertexFloats], back.mesh.vertices[a * kVertexFloats + 1], back.mesh.vertices[a * kVertexFloats + 2]);
        const glm::vec3 pb(back.mesh.vertices[b * kVertexFloats], back.mesh.vertices[b * kVertexFloats + 1], back.mesh.vertices[b * kVertexFloats + 2]);
        const glm::vec3 pc(back.mesh.vertices[c * kVertexFloats], back.mesh.vertices[c * kVertexFloats + 1], back.mesh.vertices[c * kVertexFloats + 2]);
        CHECK(glm::cross(pb - pa, pc - pa).y < 0.0f);
    }

    // Splat rules.
    SplatRules rules;
    CHECK(dominantLayer(splatWeights(30.0f, 0.99f, rules)) == TerrainLayer::Grass);
    CHECK(dominantLayer(splatWeights(30.0f, 0.1f, rules)) == TerrainLayer::Rock);
    CHECK(dominantLayer(splatWeights(1.0f, 0.99f, rules)) == TerrainLayer::Sand);
    CHECK(dominantLayer(splatWeights(150.0f, 0.99f, rules)) == TerrainLayer::Snow);
    const auto baked = bakeTileSplat(config, key, 8);
    CHECK(baked.size() == 8 * 8 * 4);
}

static void testHeightfieldQueries() {
    const auto ramp = rampField(33, 2.0f, 0.5f);
    CHECK(std::abs(heightfieldHeightAt(*ramp, 10.0f, 7.0f) - 5.0f) < 1e-4f);
    CHECK(std::abs(heightfieldNormalAt(*ramp, 10.0f, 7.0f).y - 1.0f / std::sqrt(1.25f)) < 1e-4f);

    // Raycast agrees with the height function on a noisy field.
    const TerrainConfig config = makeConfig();
    const auto field = buildTileHeightfield(config, TileKey{0, 0, 0});
    Pcg32 rng(5);
    int agree = 0;
    int total = 0;
    for (int i = 0; i < 200; ++i) {
        const float x = rng.range(1.0f, 62.0f);
        const float z = rng.range(1.0f, 62.0f);
        const float top = field->maxHeight + 5.0f;
        const auto hit = heightfieldRaycast(*field, glm::vec3(x, top, z), glm::vec3(0.0f, -1.0f, 0.0f), 1000.0f);
        ++total;
        if (hit && std::abs(top - hit->distance - heightfieldHeightAt(*field, x, z)) < 1e-2f) ++agree;
    }
    CHECK(agree == total);
    // Oblique rays: the hit point lies on the surface.
    int surfaceHits = 0;
    int obliqueTotal = 0;
    for (int i = 0; i < 200; ++i) {
        const glm::vec3 origin(rng.range(-20.0f, 80.0f), field->maxHeight + 10.0f, rng.range(-20.0f, 80.0f));
        const glm::vec3 direction = glm::normalize(glm::vec3(rng.range(-1.0f, 1.0f), -rng.range(0.2f, 1.0f), rng.range(-1.0f, 1.0f)));
        const auto hit = heightfieldRaycast(*field, origin, direction, 2000.0f);
        if (!hit) continue;
        ++obliqueTotal;
        const glm::vec3 point = origin + direction * hit->distance;
        if (std::abs(point.y - heightfieldHeightAt(*field, point.x, point.z)) < 0.02f) ++surfaceHits;
    }
    CHECK(obliqueTotal > 20);
    CHECK(surfaceHits == obliqueTotal);
    CHECK(!heightfieldRaycast(*field, glm::vec3(10.0f, field->maxHeight + 5.0f, 10.0f), glm::vec3(0.0f, 1.0f, 0.0f), 100.0f).has_value());
    CHECK(!heightfieldRaycast(*field, glm::vec3(10.0f, field->maxHeight + 5.0f, 10.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1.0f).has_value());

    // Sphere contact and sweep on the ramp.
    const auto contact = heightfieldSphereContact(*ramp, glm::vec3(10.0f, 5.2f, 7.0f), 0.5f);
    CHECK(contact.has_value());
    CHECK(contact && contact->depth > 0.0f && contact->normal.y > 0.8f);
    CHECK(!heightfieldSphereContact(*ramp, glm::vec3(10.0f, 8.0f, 7.0f), 0.5f).has_value());
    const auto buried = heightfieldSphereContact(*ramp, glm::vec3(10.0f, 3.0f, 7.0f), 0.5f);
    CHECK(buried && buried->depth > 1.0f && buried->normal.y > 0.8f);

    const auto sweep = heightfieldSweepSphere(*ramp, glm::vec3(10.0f, 15.0f, 7.0f), glm::vec3(0.0f, -1.0f, 0.0f), 50.0f, 0.5f);
    CHECK(sweep.has_value());
    if (sweep) {
        const glm::vec3 centre(10.0f, 15.0f - sweep->distance, 7.0f);
        const auto touch = heightfieldSphereContact(*ramp, centre + glm::vec3(0.0f, -0.01f, 0.0f), 0.5f);
        CHECK(touch.has_value());
        CHECK(!heightfieldSphereContact(*ramp, centre + glm::vec3(0.0f, 0.05f, 0.0f), 0.5f).has_value());
        CHECK(sweep->normal.y > 0.8f);
    }
    CHECK(!heightfieldSweepSphere(*ramp, glm::vec3(10.0f, 15.0f, 7.0f), glm::vec3(0.0f, -1.0f, 0.0f), 2.0f, 0.5f).has_value());
    CHECK(!heightfieldSweepSphere(*ramp, glm::vec3(10.0f, 15.0f, 7.0f), glm::vec3(1.0f, 0.0f, 0.0f), 5.0f, 0.5f).has_value());
    const auto side = heightfieldSweepSphere(*ramp, glm::vec3(0.5f, 6.0f, 7.0f), glm::vec3(1.0f, 0.0f, 0.0f), 30.0f, 0.5f);
    CHECK(side.has_value());
    CHECK(heightfieldSweepSphere(*ramp, glm::vec3(10.0f, 4.0f, 7.0f), glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, 0.5f)->distance == 0.0f);
}

static void testWorldPhysics() {
    World w;
    setup(w);
    const auto ramp = rampField(65, 1.0f, 0.3f);
    const EntityHandle ground = addField(w, ramp, glm::vec3(-10.0f, 2.0f, -10.0f));

    const auto ray = physicsRaycast(w, glm::vec3(10.0f, 100.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(ray.has_value());
    CHECK(ray && ray->entity == ground);
    CHECK(ray && std::abs(100.0f - ray->distance - (2.0f + 0.3f * 20.0f)) < 1e-3f);
    const auto sweep = physicsSweepSphere(w, glm::vec3(10.0f, 100.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 200.0f, 0.5f);
    CHECK(sweep && sweep->distance < ray->distance && sweep->distance > ray->distance - 1.0f);
    CHECK(!physicsRaycast(w, glm::vec3(10.0f, 100.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 200.0f, [&](EntityHandle e) { return e == ground; }).has_value());
    const auto overlap = physicsOverlapCapsule(w, glm::vec3(10.0f, 8.0f, 0.0f), glm::vec3(10.0f, 9.5f, 0.0f), 0.35f);
    CHECK(!overlap.empty());

    // Spheres and boxes fall and rest on it.
    EntityDesc d;
    d.position = glm::vec3(10.0f, 20.0f, 0.0f);
    const EntityHandle ball = w.create(d);
    w.add<SphereCollider>(ball, SphereCollider{0.5f});
    w.add<RigidBody>(ball, RigidBody{});
    d.position = glm::vec3(15.0f, 20.0f, 3.0f);
    const EntityHandle box = w.create(d);
    w.add<BoxCollider>(box, BoxCollider{glm::vec3(0.5f)});
    RigidBody boxBody;
    boxBody.restitution = 0.0f;
    w.add<RigidBody>(box, boxBody);
    int collisions = 0;
    w.events().subscribe<Collision>([&](const Collision& c) { if (c.b == ground || c.a == ground) ++collisions; });
    PhysicsSettings settings;
    for (int i = 0; i < 180; ++i) {
        stepPhysics(w, 1.0f / 60.0f, settings);
        w.events().dispatch();
    }
    const glm::vec3 ballPosition = w.getWorldPosition(ball);
    const float ballSurface = 2.0f + 0.3f * (ballPosition.x + 10.0f);
    CHECK(ballPosition.y >= ballSurface + 0.5f - 0.05f);
    CHECK(ballPosition.y < ballSurface + 0.5f / 0.9578f + 0.2f);
    const glm::vec3 boxPosition = w.getWorldPosition(box);
    const float boxSurface = 2.0f + 0.3f * (boxPosition.x + 10.0f);
    CHECK(boxPosition.y > boxSurface - 0.1f);
    CHECK(boxPosition.y < boxSurface + 1.5f);
    CHECK(collisions > 0);
}

static void testCharacter() {
    World w;
    setup(w);
    PortalSystem p;
    PhysicsSettings ps;
    const auto ramp = rampField(101, 1.0f, 0.3f);
    addField(w, ramp, glm::vec3(-20.0f, 0.0f, -50.0f));
    CharacterController c(w, p, ps);
    c.spawn(glm::vec3(-10.0f, 0.0f, 0.0f), 0.0f);
    for (int i = 0; i < 120; ++i) {
        c.setInput({glm::vec2(0.0f, 0.0f), false, false});
        c.update(1.0f / 60.0f);
    }
    CHECK(c.grounded());
    for (int i = 0; i < 480; ++i) {
        c.setInput({glm::vec2(0.0f, 1.0f), false, false});
        c.update(1.0f / 60.0f);
    }
    const float surface = 0.3f * (c.position().x + 20.0f);
    CHECK(c.position().x > -6.0f);
    CHECK(c.grounded());
    CHECK(std::abs(c.position().y - surface) < 0.15f);
    CHECK(c.position().y > 3.0f);
}

static void testScatter() {
    const TerrainConfig config = makeConfig();
    ScatterConfig scatter;
    scatter.seed = 99;
    ScatterSpecies tree;
    tree.name = "tree";
    tree.kind = ScatterKind::Tree;
    tree.mesh = 5;
    tree.spacing = 6.0f;
    tree.density = 0.8f;
    tree.minNormalY = 0.85f;
    tree.minHeight = 2.0f;
    tree.maxHeight = 90.0f;
    ScatterSpecies rock;
    rock.name = "rock";
    rock.kind = ScatterKind::Rock;
    rock.mesh = 6;
    rock.spacing = 10.0f;
    rock.density = 0.7f;
    rock.minNormalY = 0.0f;
    rock.alignToSlope = true;
    scatter.species = {tree, rock};

    const auto first = scatterTile(config, scatter, TileKey{0, 0, 1});
    const auto second = scatterTile(config, scatter, TileKey{0, 0, 1});
    CHECK(!first.empty());
    CHECK(first.size() == second.size());
    bool identical = first.size() == second.size();
    for (size_t i = 0; i < first.size() && identical; ++i) identical = first[i].position == second[i].position && first[i].scale == second[i].scale && first[i].yaw == second[i].yaw;
    CHECK(identical);

    // Union of the four level-0 children equals the level-1 tile.
    size_t childTotal = 0;
    std::set<std::string> childKeys;
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            const auto part = scatterTile(config, scatter, TileKey{x, z, 0});
            childTotal += part.size();
            for (const ScatterInstance& s : part) childKeys.insert(std::to_string(s.species) + ":" + std::to_string(static_cast<int32_t>(s.cellX)) + "_" + std::to_string(static_cast<int32_t>(s.cellZ)));
        }
    }
    CHECK(childTotal == first.size());
    CHECK(childKeys.size() == first.size());

    scatter.seed = 100;
    const auto reseeded = scatterTile(config, scatter, TileKey{0, 0, 1});
    CHECK(reseeded.size() != first.size() || reseeded[0].position != first[0].position);
    scatter.seed = 99;

    bool constraints = true;
    bool inside = true;
    for (const ScatterInstance& s : first) {
        const ScatterSpecies& sp = scatter.species[s.species];
        constraints = constraints && s.scale >= sp.minScale && s.scale <= sp.maxScale;
        if (sp.name == "tree") constraints = constraints && s.normal.y >= sp.minNormalY - 1e-4f && s.position.y >= sp.minHeight && s.position.y <= sp.maxHeight;
        inside = inside && s.position.x >= 0.0f && s.position.x < 128.0f && s.position.z >= 0.0f && s.position.z < 128.0f;
    }
    CHECK(constraints);
    CHECK(inside);

    const auto entities = scatterEntities(scatter, TileKey{0, 0, 1}, first);
    CHECK(entities.size() == first.size());
    std::set<std::string> keys;
    for (const CellEntity& e : entities) keys.insert(e.key);
    CHECK(keys.size() == entities.size());
    CHECK(entities[0].desc.mesh == scatter.species[first[0].species].mesh);

    // Biome gating: a species restricted to an empty biome band yields nothing.
    ScatterConfig none = scatter;
    none.species[0].minBiome = 2.0f;
    none.species[0].maxBiome = 3.0f;
    none.species.resize(1);
    CHECK(scatterTile(config, none, TileKey{0, 0, 1}).empty());
}

static void testLoader() {
    World w;
    setup(w);
    TerrainLoaderConfig config;
    config.terrain = makeConfig();
    ScatterSpecies tree;
    tree.name = "tree";
    tree.mesh = 3;
    tree.spacing = 4.0f;
    tree.density = 0.9f;
    tree.minNormalY = 0.0f;
    config.scatter.species = {tree};
    uint32_t uploads = 0;
    config.uploadMesh = [&](const Mesh& m) {
        CHECK(!m.vertices.empty());
        return 100 + uploads++;
    };
    config.tileOfCell = [](int cell) -> std::optional<TileKey> {
        if (cell < 0 || cell >= 4) return std::nullopt;
        return TileKey{cell % 2, cell / 2, 0};
    };
    config.scatterMaxLevel = 0;
    CellLoader loader = makeTerrainLoader(w, config);

    AsyncJobs jobs(2);
    std::vector<std::shared_ptr<void>> payloads(4);
    for (int cell = 0; cell < 4; ++cell) payloads[static_cast<size_t>(cell)] = loader.prepare(cell);
    CHECK(loader.prepare(9) == nullptr);

    const size_t before = w.aliveCount();
    int steps = 0;
    for (int cell = 0; cell < 4; ++cell) {
        for (;;) {
            StreamBudget budget;
            budget.millis = 1000.0;
            budget.bytes = 4096;
            ++steps;
            if (loader.commit(cell, payloads[static_cast<size_t>(cell)].get(), budget)) break;
            if (steps > 100000) break;
        }
    }
    CHECK(steps > 4);
    CHECK(uploads == 4);
    CHECK(w.aliveCount() > before + 4);
    const auto hit = physicsRaycast(w, glm::vec3(10.0f, 500.0f, 10.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(hit.has_value());
    CHECK(hit && std::abs(500.0f - hit->distance - terrainHeight(config.terrain, 10.0f, 10.0f)) < 0.05f);
    const auto hit2 = physicsRaycast(w, glm::vec3(100.0f, 500.0f, 100.0f), glm::vec3(0.0f, -1.0f, 0.0f));
    CHECK(hit2 && std::abs(500.0f - hit2->distance - terrainHeight(config.terrain, 100.0f, 100.0f)) < 0.05f);

    StreamBudget unlimited;
    unlimited.unlimited = true;
    for (int cell = 0; cell < 4; ++cell) {
        StreamBudget tiny;
        tiny.millis = 1000.0;
        tiny.bytes = 100;
        int guard = 0;
        while (!loader.unload(cell, tiny) && ++guard < 100000) tiny = StreamBudget{.millis = 1000.0, .bytes = 100};
        CHECK(guard < 100000);
    }
    CHECK(w.aliveCount() == before);
    CHECK(!physicsRaycast(w, glm::vec3(10.0f, 500.0f, 10.0f), glm::vec3(0.0f, -1.0f, 0.0f)).has_value());

    // A worker-thread prepare works too.
    std::shared_ptr<void> async;
    jobs.submit(0, [&] { async = loader.prepare(1); });
    jobs.waitIdle();
    CHECK(async != nullptr);
}


static bool areaCovered(const std::vector<SelectedTile>& tiles, const TerrainConfig& config, float x, float z) {
    for (const SelectedTile& tile : tiles) {
        const float size = tileWorldSize(config, tile.key);
        const float x0 = static_cast<float>(tile.key.x) * size;
        const float z0 = static_cast<float>(tile.key.z) * size;
        if (x >= x0 && x < x0 + size && z >= z0 && z < z0 + size) return true;
    }
    return false;
}

static void testBalanceAndFrustum() {
    const TerrainConfig config = makeConfig();
    LodParams lod;
    lod.maxLevel = 4;
    lod.viewDistance = 1500.0f;
    lod.balance = false;
    const glm::vec3 camera(300.0f, 40.0f, -120.0f);
    const std::vector<SelectedTile> raw = selectTerrainTiles(config, camera, lod);
    lod.balance = true;
    const std::vector<SelectedTile> balanced = selectTerrainTiles(config, camera, lod);
    CHECK(balanced.size() >= raw.size());

    // 2:1: sample points just across every edge midpoint, the neighbouring leaf is at most one level apart.
    int violations = 0;
    const auto levelAt = [&](float x, float z) {
        for (const SelectedTile& tile : balanced) {
            const float size = tileWorldSize(config, tile.key);
            const float x0 = static_cast<float>(tile.key.x) * size;
            const float z0 = static_cast<float>(tile.key.z) * size;
            if (x >= x0 && x < x0 + size && z >= z0 && z < z0 + size) return tile.key.level;
        }
        return -1;
    };
    for (const SelectedTile& tile : balanced) {
        const float size = tileWorldSize(config, tile.key);
        const float cx = static_cast<float>(tile.key.x) * size + size * 0.5f;
        const float cz = static_cast<float>(tile.key.z) * size + size * 0.5f;
        const float probes[4][2] = {{cx, cz - size * 0.5f - 0.5f}, {cx, cz + size * 0.5f + 0.5f}, {cx - size * 0.5f - 0.5f, cz}, {cx + size * 0.5f + 0.5f, cz}};
        for (const auto& probe : probes) {
            const int other = levelAt(probe[0], probe[1]);
            if (other >= 0 && std::abs(other - tile.key.level) > 1) ++violations;
        }
    }
    CHECK(violations == 0);
    // Without balance the plain distance rule produces a larger jump somewhere in a big enough field.
    // Full coverage: every point near the camera is covered exactly once.
    CHECK(areaCovered(balanced, config, camera.x, camera.z));

    // Frustum culling keeps tiles in front, drops tiles far behind, keeps tiles near the camera.
    const glm::mat4 view = glm::lookAt(camera, camera + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.5f, 3000.0f);
    lod.frustumCull = true;
    lod.planes = frustumPlanes(projection * view);
    lod.frustumMargin = 0.0f;
    lod.keepRadius = 100.0f;
    const std::vector<SelectedTile> culled = selectTerrainTiles(config, camera, lod);
    CHECK(culled.size() < balanced.size());
    CHECK(areaCovered(culled, config, camera.x, camera.z + 400.0f));
    CHECK(!areaCovered(culled, config, camera.x, camera.z - 900.0f));
    CHECK(areaCovered(culled, config, camera.x, camera.z));
    CHECK(boxInFrustum(lod.planes, glm::vec3(camera.x - 5.0f, 0.0f, camera.z + 100.0f), glm::vec3(camera.x + 5.0f, 10.0f, camera.z + 110.0f)));
    CHECK(!boxInFrustum(lod.planes, glm::vec3(camera.x - 5.0f, 0.0f, camera.z - 300.0f), glm::vec3(camera.x + 5.0f, 10.0f, camera.z - 290.0f)));
}

static void testPackedMorph() {
    const TerrainConfig config = makeConfig();
    TileMeshOptions packed;
    packed.packMorph = true;
    const TileKey key{2, -1, 2};
    const TerrainTileMesh plain = buildTileMesh(config, key);
    const TerrainTileMesh withMorph = buildTileMesh(config, key, packed);
    CHECK(plain.mesh.vertices.size() == withMorph.mesh.vertices.size());
    bool lengthsMatch = true;
    bool directionsMatch = true;
    for (size_t v = 0; v < withMorph.morph.size(); ++v) {
        const float* a = &plain.mesh.vertices[v * kVertexFloats];
        const float* b = &withMorph.mesh.vertices[v * kVertexFloats];
        const glm::vec3 na(a[3], a[4], a[5]);
        const glm::vec3 nb(b[3], b[4], b[5]);
        lengthsMatch = lengthsMatch && std::abs((glm::length(nb) - 1.0f) * kTerrainMorphScale - withMorph.morph[v]) < 0.01f;
        directionsMatch = directionsMatch && glm::dot(glm::normalize(nb), glm::normalize(na)) > 0.9999f;
    }
    CHECK(lengthsMatch);
    CHECK(directionsMatch);
}

static void testStreamer() {
    World w;
    setup(w);
    TerrainLoaderConfig loader;
    loader.terrain = makeConfig();
    loader.scatter.seed = 5;
    ScatterSpecies tree;
    tree.name = "tree";
    tree.spacing = 10.0f;
    tree.density = 0.8f;
    tree.minNormalY = 0.7f;
    // Tiny tetra as the CPU geometry: merged into one mesh per tile and species.
    tree.geometry = std::make_shared<const Mesh>(Mesh(std::vector<float>{0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 1}, std::vector<unsigned int>{0, 1, 2, 0, 2, 3, 0, 3, 1}));
    loader.scatter.species.push_back(tree);
    loader.scatterMaxLevel = 1;
    auto uploads = std::make_shared<int>(0);
    auto released = std::make_shared<std::vector<uint32_t>>();
    loader.uploadMesh = [uploads](const Mesh&) { return static_cast<uint32_t>(++*uploads); };
    loader.uploadScatterMesh = [uploads](PreparedMesh&) { return static_cast<uint32_t>(++*uploads); };
    loader.releaseMesh = [released](uint32_t id) { released->push_back(id); };
    TerrainStreamConfig stream;
    stream.lod.maxLevel = 3;
    stream.lod.viewDistance = 700.0f;
    stream.evictDelay = 5;
    stream.reselectInterval = 1;
    TerrainStreamer streamer(w, std::move(loader), stream);
    const glm::vec3 forward(1.0f, 0.0f, 0.0f);
    streamer.prime(glm::vec3(100.0f, 30.0f, 100.0f), forward, nullptr);
    CHECK(streamer.settled());
    CHECK(streamer.stats().resident > 4);
    CHECK(*uploads > static_cast<int>(streamer.stats().resident));  // tile meshes plus merged scatter batches
    // One entity per tile and species at most, not one per instance.
    CHECK(streamer.stats().entities <= streamer.stats().resident * 2);
    CHECK(streamer.visibleTiles().size() == streamer.wantedTiles().size());
    // Visible tiles never overlap.
    bool overlap = false;
    const std::vector<TileKey> visible = streamer.visibleTiles();
    for (const TileKey& a : visible) {
        for (const TileKey& b : visible) {
            if (a.level > b.level && ((b.x >> (a.level - b.level)) == a.x) && ((b.z >> (a.level - b.level)) == a.z)) overlap = true;
        }
    }
    CHECK(!overlap);
    // Move far away: old tiles are evicted after the delay and their GPU meshes released.
    for (int i = 0; i < 400 && released->empty(); ++i) {
        streamer.update(glm::vec3(2500.0f + static_cast<float>(i), 30.0f, 100.0f), forward, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    for (int i = 0; i < 200; ++i) {
        streamer.update(glm::vec3(2900.0f, 30.0f, 100.0f), forward, nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(streamer.stats().evicted > 0);
    CHECK(!released->empty());
    CHECK(streamer.settled());
}

int main() {
    testNoise();
    testHeightmap();
    testLod();
    testMesh();
    testHeightfieldQueries();
    testWorldPhysics();
    testCharacter();
    testScatter();
    testLoader();
    testBalanceAndFrustum();
    testPackedMorph();
    testStreamer();
    return testsupport::finish();
}
