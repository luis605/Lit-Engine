#include "TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <vector>

import Engine.World;
import Engine.Instancing;
import Engine.Render.entity;
import Engine.Rng;
import Engine.glm;

namespace {

std::vector<InstanceData> field(uint32_t count, float extent, uint64_t seed) {
    Pcg32 rng(seed, 3);
    std::vector<InstanceData> out(count);
    for (uint32_t i = 0; i < count; ++i) {
        InstanceData& d = out[i];
        d.position = glm::vec3((rng.uniform() - 0.5f) * extent, rng.uniform() * 2.0f, (rng.uniform() - 0.5f) * extent);
        d.scale = 0.5f + rng.uniform();
        d.rotation = glm::angleAxis(rng.uniform() * 6.2831853f, glm::vec3(0.0f, 1.0f, 0.0f));
        d.seed = i * 2654435761u;
    }
    return out;
}

void testAllocator() {
    InstanceAllocator pool(100);
    const auto a = pool.allocate(10);
    const auto b = pool.allocate(20);
    const auto c = pool.allocate(5);
    CHECK(a && b && c);
    CHECK(*a == 0 && *b == 10 && *c == 30);
    CHECK(pool.highWater() == 35 && pool.usedCells() == 35);
    pool.free(*b, 20);
    CHECK(pool.freeRanges() == 1 && pool.usedCells() == 15);
    const auto d = pool.allocate(8);
    CHECK(d && *d == 10);
    const auto e = pool.allocate(12);
    CHECK(e && *e == 18);
    CHECK(pool.freeRanges() == 0 && pool.highWater() == 35);
    pool.free(*c, 5);
    CHECK(pool.highWater() == 30);
    pool.free(*d, 8);
    pool.free(*e, 12);
    CHECK(pool.highWater() == 10 && pool.freeRanges() == 0);
    CHECK(!pool.allocate(91));
    CHECK(pool.allocate(90).has_value());
    CHECK(!pool.allocate(1));
    CHECK(!pool.allocate(0));
    pool.clear();
    CHECK(pool.highWater() == 0 && pool.usedCells() == 0);

    InstanceAllocator random(4096);
    Pcg32 rng(9, 1);
    std::vector<std::pair<uint32_t, uint32_t>> live;
    bool overlap = false;
    for (int step = 0; step < 4000; ++step) {
        if (live.empty() || rng.uniform() < 0.55f) {
            const uint32_t size = 1 + rng.below(40);
            if (const auto first = random.allocate(size)) {
                for (const auto& [f, n] : live) overlap = overlap || (*first < f + n && f < *first + size);
                live.emplace_back(*first, size);
            }
        } else {
            const size_t index = rng.below(static_cast<uint32_t>(live.size()));
            random.free(live[index].first, live[index].second);
            live.erase(live.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    CHECK(!overlap);
    uint32_t used = 0;
    for (const auto& [f, n] : live) used += n;
    CHECK(random.usedCells() == used);
    for (const auto& [f, n] : live) random.free(f, n);
    CHECK(random.highWater() == 0 && random.freeRanges() == 0 && random.usedCells() == 0);
}

void testBatchEdits() {
    InstanceBatch batch;
    std::vector<InstanceData> data = field(10, 10.0f, 1);
    batch.assign(data);
    batch.keys = {100, 101, 102, 103, 104, 105, 106, 107, 108, 109};
    const uint64_t layout = batch.layoutVersion;
    CHECK(batch.aliveCount() == 10 && !batch.dirty());

    const uint64_t version = batch.version;
    InstanceData moved = data[3];
    moved.position += glm::vec3(1.0f, 0.0f, 0.0f);
    batch.update(3, std::span<const InstanceData>(&moved, 1));
    CHECK(batch.version > version && batch.layoutVersion == layout);
    CHECK(batch.dirty() && batch.dirtyFirst == 3 && batch.dirtyEnd == 4);
    batch.update(7, std::span<const InstanceData>(&moved, 1));
    CHECK(batch.dirtyFirst == 3 && batch.dirtyEnd == 8);
    batch.clearDirty();

    CHECK(batch.remove(5));
    CHECK(!batch.remove(5));
    CHECK(!batch.remove(10));
    CHECK(batch.aliveCount() == 9 && batch.instances.size() == 10);
    CHECK((batch.instances[5].flags & InstanceFlags::Dead) != 0);
    CHECK(batch.dirtyFirst == 5 && batch.dirtyEnd == 6 && batch.layoutVersion == layout);
    CHECK(batch.indexOfKey(105) == 5u);
    CHECK(batch.revive(5) && batch.aliveCount() == 10);

    const InstanceData last = batch.instances[9];
    const auto movedKey = batch.swapRemove(2);
    CHECK(movedKey == 109u);
    CHECK(batch.instances.size() == 9 && batch.keys.size() == 9);
    CHECK(batch.instances[2] == last && batch.keys[2] == 109);
    CHECK(!batch.indexOfKey(102));
    CHECK(batch.indexOfKey(109) == 2u);
    CHECK(batch.layoutVersion > layout);
    CHECK(!batch.swapRemove(8).has_value());
    CHECK(batch.instances.size() == 8);
    std::set<uint64_t> keys(batch.keys.begin(), batch.keys.end());
    CHECK(keys.size() == 8);

    const uint64_t before = batch.layoutVersion;
    batch.update(10, std::span<const InstanceData>(&moved, 1));
    CHECK(batch.instances.size() == 11 && batch.keys.size() == 11 && batch.layoutVersion > before);
    InstanceBatchSettings settings;
    settings.drawDistance = 50.0f;
    const uint64_t s0 = batch.layoutVersion;
    batch.setSettings(settings);
    CHECK(batch.layoutVersion > s0);
    batch.setSettings(settings);
    CHECK(batch.layoutVersion == s0 + 1);
}

void testPacking() {
    InstanceData d;
    d.position = glm::vec3(12.5f, -3.0f, 1000.25f);
    d.scale = 1.75f;
    d.rotation = glm::normalize(glm::quat(0.3f, -0.5f, 0.7f, 0.1f));
    d.color = 0x80FF2010u;
    d.seed = 0xABCDEF12u;
    d.flags = InstanceFlags::NoShadow;
    const InstanceData back = unpackInstance(packInstance(d));
    CHECK(back.position == d.position && back.scale == d.scale);
    CHECK(std::abs(std::abs(glm::dot(back.rotation, d.rotation)) - 1.0f) < 1.0e-4f);
    CHECK(back.color == d.color);
    CHECK(back.seed == (d.seed & 0xFFFFFFu));
    CHECK(back.flags == InstanceFlags::NoShadow);
    const glm::vec4 c = instanceColorFloat(packInstanceColor(glm::vec4(0.25f, 0.5f, 1.0f, 0.0f)));
    CHECK(std::abs(c.r - 0.25f) < 0.01f && std::abs(c.g - 0.5f) < 0.01f && c.b == 1.0f && c.a == 0.0f);
    float sum = 0.0f;
    for (uint32_t s = 0; s < 10000; ++s) {
        const float h = instanceHash01(s);
        CHECK(h >= 0.0f && h < 1.0f);
        sum += h;
    }
    CHECK(std::abs(sum / 10000.0f - 0.5f) < 0.02f);
}

void testCells() {
    const std::vector<InstanceData> data = field(5000, 400.0f, 4);
    const std::vector<uint32_t> order = instanceSpatialOrder(data);
    std::vector<uint32_t> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    std::vector<uint32_t> identity(data.size());
    std::iota(identity.begin(), identity.end(), 0u);
    CHECK(sorted == identity);
    const glm::vec4 meshSphere(0.0f, 1.0f, 0.0f, 1.5f);
    float spatialRadius = 0.0f;
    float naiveRadius = 0.0f;
    bool contained = true;
    size_t cells = 0;
    for (size_t first = 0; first < order.size(); first += kInstanceCellSize, ++cells) {
        const size_t count = std::min<size_t>(kInstanceCellSize, order.size() - first);
        const InstanceCellGpu cell = buildInstanceCell(data, std::span<const uint32_t>(order.data() + first, count), meshSphere, 7);
        CHECK(cell.info[0] == 7 && cell.info[1] == count && cell.info[2] == count);
        for (size_t i = first; i < first + count; ++i) {
            const InstanceData& d = data[order[i]];
            const glm::vec3 center = d.position + d.rotation * (glm::vec3(meshSphere) * d.scale);
            contained = contained && glm::length(center - glm::vec3(cell.sphere)) + meshSphere.w * d.scale <= cell.sphere.w + 1.0e-3f;
        }
        spatialRadius += cell.sphere.w;
        const InstanceCellGpu naive = buildInstanceCell(data, std::span<const uint32_t>(identity.data() + first, count), meshSphere, 7);
        naiveRadius += naive.sphere.w;
    }
    CHECK(contained);
    CHECK(spatialRadius * 3.0f < naiveRadius);
    std::printf("cells %zu: mean radius spatial %.1f m, insertion order %.1f m\n", cells, spatialRadius / static_cast<float>(cells), naiveRadius / static_cast<float>(cells));

    std::vector<InstanceData> dead = data;
    for (InstanceData& d : dead) d.flags |= InstanceFlags::Dead;
    const InstanceCellGpu empty = buildInstanceCell(dead, std::span<const uint32_t>(order.data(), kInstanceCellSize), meshSphere, 0);
    CHECK(empty.info[2] == 0 && empty.sphere.w < 0.0f);
}

void testReferenceCull() {
    InstanceBatch batch;
    batch.assign(field(20000, 1000.0f, 8));
    for (uint32_t i = 0; i < batch.instances.size(); i += 7) batch.remove(i);
    const glm::vec4 meshSphere(0.0f, 1.0f, 0.0f, 1.0f);
    const glm::mat4 owner = glm::translate(glm::mat4(1.0f), glm::vec3(100.0f, 0.0f, -50.0f));
    InstanceCullView view;
    view.position = glm::vec3(100.0f, 10.0f, 300.0f);
    view.view = glm::lookAt(view.position, glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 2000.0f);
    view.planes = instanceFrustumPlanes(projection * view.view);
    view.pixelScale = projection[1][1] * 720.0f;

    const InstanceCullResult cells = cullInstancesReference(batch, owner, meshSphere, view);
    InstanceCullView flat = view;
    flat.cellCull = false;
    const InstanceCullResult brute = cullInstancesReference(batch, owner, meshSphere, flat);
    CHECK(cells.visible == brute.visible && cells.perLod == brute.perLod);
    CHECK(cells.visible > 100 && cells.visible < brute.instancesTested);
    CHECK(cells.instancesTested < brute.instancesTested && cells.instancesTested >= cells.visible);
    CHECK(brute.instancesTested == batch.aliveCount());
    std::printf("reference cull: %u visible of %u live; cells %u/%u accepted, %u instances tested (flat %u)\n", cells.visible, batch.aliveCount(), cells.cellsAccepted, cells.cellsTested, cells.instancesTested, brute.instancesTested);
    uint32_t lodSum = 0;
    for (const uint32_t n : cells.perLod) lodSum += n;
    CHECK(lodSum == cells.visible);
    CHECK(cells.perLod[0] > 0 && cells.perLod[3] + cells.perLod[4] + cells.perLod[5] + cells.perLod[6] > 0);

    batch.settings.drawDistance = 300.0f;
    const InstanceCullResult cut = cullInstancesReference(batch, owner, meshSphere, view);
    batch.settings.fadeStart = 150.0f;
    const InstanceCullResult faded = cullInstancesReference(batch, owner, meshSphere, view);
    CHECK(cut.visible < cells.visible && faded.visible < cut.visible && faded.visible > cut.visible / 4);
    view.distanceScale = 2.0f;
    const InstanceCullResult scaled = cullInstancesReference(batch, owner, meshSphere, view);
    CHECK(scaled.visible > faded.visible);
    view.distanceScale = 1.0f;
    view.forcedLod = 2;
    const InstanceCullResult forced = cullInstancesReference(batch, owner, meshSphere, view);
    CHECK(forced.perLod[2] == forced.visible);
}

void testComponent() {
    World world;
    registerCoreComponents(world);
    const EntityHandle entity = world.create("batch");
    InstanceBatch batch;
    batch.mesh = 14;
    batch.material = 3;
    batch.shader = 2;
    batch.settings.drawDistance = 250.0f;
    batch.settings.fadeStart = 200.0f;
    batch.settings.castShadows = false;
    batch.assign(field(50, 30.0f, 2));
    batch.keys.resize(50);
    std::iota(batch.keys.begin(), batch.keys.end(), 1000ull);
    batch.remove(4);
    world.add<InstanceBatch>(entity, batch);
    const std::optional<std::string> text = world.componentText(entity, "InstanceBatch");
    CHECK(text.has_value());
    const auto parsed = instanceBatchFromText(text.value_or(""));
    CHECK(parsed.has_value());
    if (parsed) {
        CHECK(parsed->mesh == 14 && parsed->material == 3 && parsed->shader == 2);
        CHECK(parsed->settings == batch.settings);
        CHECK(parsed->instances.size() == 50 && parsed->keys == batch.keys);
        CHECK(parsed->aliveCount() == 49 && (parsed->instances[4].flags & InstanceFlags::Dead) != 0);
        bool same = true;
        for (size_t i = 0; i < 50; ++i) same = same && glm::length(parsed->instances[i].position - batch.instances[i].position) < 1.0e-4f && parsed->instances[i].seed == batch.instances[i].seed;
        CHECK(same);
    }
    const std::string snapshot = world.snapshot();
    World other;
    registerCoreComponents(other);
    CHECK(other.restore(snapshot));
    const EntityHandle restored = other.find("batch");
    const InstanceBatch* copy = other.get<InstanceBatch>(restored);
    CHECK(copy && copy->instances.size() == 50 && copy->aliveCount() == 49);
    CHECK(world.setComponentFromText(entity, "InstanceBatch", "5 1 0 0 0 1 1 1 0  1 2 3 1.5 1 0 0 0 4294967295 7 0"));
    const InstanceBatch* edited = world.get<InstanceBatch>(entity);
    CHECK(edited && edited->mesh == 5 && edited->instances.size() == 1 && edited->instances[0].position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(!instanceBatchFromText("1 2 3").has_value());
}

}

int main() {
    testAllocator();
    testBatchEdits();
    testPacking();
    testCells();
    testReferenceCull();
    testComponent();
    return testsupport::finish();
}
