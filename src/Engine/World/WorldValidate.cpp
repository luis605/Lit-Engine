module;

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.glm;

namespace {

constexpr size_t kMaxProblems = 64;
constexpr float kCacheToleranceBase = 1.0e-3f;

std::string text(size_t value) { return std::to_string(value); }

}

struct World::Validator {
    World& world;
    size_t slotCount;
    size_t flaggedAlive = 0;
    std::vector<std::string> problems;
    std::vector<uint8_t> pendingSpatial;

    explicit Validator(World& worldToCheck) : world(worldToCheck), slotCount(worldToCheck.m_alive.size()) {}

    void fail(std::string message) {
        if (problems.size() < kMaxProblems) problems.push_back(std::move(message));
    }

    bool checkArraySizes() {
        const size_t sizes[] = {world.m_generation.size(), world.m_visible.size(), world.m_mesh.size(), world.m_layer.size(), world.m_position.size(), world.m_tags.size(), world.m_names.size(), world.m_db.firstChild.size(), world.m_db.nextSibling.size(), world.m_prevSibling.size(), world.m_db.transforms.size(), world.m_db.hierarchies.size(), world.m_db.renderables.size()};
        for (size_t size : sizes) {
            if (size != slotCount) {
                fail("per-slot arrays disagree on size");
                return false;
            }
        }
        return true;
    }

    void checkAliveCountAndFreeList() {
        for (size_t slot = 0; slot < slotCount; ++slot) flaggedAlive += world.m_alive[slot] ? 1 : 0;
        if (flaggedAlive != world.m_aliveCount) fail("alive count " + text(world.m_aliveCount) + " differs from flagged " + text(flaggedAlive));

        std::vector<uint8_t> inFreeList(slotCount, 0);
        for (Entity freeSlot : world.m_freeList) {
            if (freeSlot >= slotCount) {
                fail("free list holds out-of-range slot " + text(freeSlot));
                continue;
            }
            if (world.m_alive[freeSlot]) fail("free list holds alive slot " + text(freeSlot));
            if (inFreeList[freeSlot]) fail("free list holds duplicate slot " + text(freeSlot));
            inFreeList[freeSlot] = 1;
        }
        for (size_t slot = 0; slot < slotCount; ++slot) {
            if (!world.m_alive[slot] && !inFreeList[slot]) fail("dead slot " + text(slot) + " missing from free list");
        }
    }

    void checkEntities() {
        for (Entity index = 0; index < slotCount; ++index) {
            if (!world.m_alive[index]) continue;
            const Entity parent = world.m_db.hierarchies[index].parent;
            if (parent != INVALID_ENTITY && (parent >= slotCount || !world.m_alive[parent] || parent == index)) fail("entity " + text(index) + " has invalid parent " + text(parent));
            if (world.m_db.renderables[index].objectId != index) fail("entity " + text(index) + " has wrong renderable objectId");
            const uint32_t expectedLevel = parent == INVALID_ENTITY || parent >= slotCount ? 0 : world.m_db.hierarchies[parent].level + 1;
            if (world.m_db.hierarchies[index].level != expectedLevel) fail("entity " + text(index) + " has level " + text(world.m_db.hierarchies[index].level) + ", expected " + text(expectedLevel));
            if (world.m_db.renderables[index].mesh_uuid != world.m_mesh[index]) fail("entity " + text(index) + " mesh mismatch with renderable");
            const bool hiddenFlag = (world.m_db.renderables[index].flags & RENDER_HIDDEN) != 0;
            if (hiddenFlag == (world.m_visible[index] != 0)) fail("entity " + text(index) + " visibility disagrees with hidden flag");
            checkParentCycle(index, parent);
        }
    }

    void checkParentCycle(Entity index, Entity parent) {
        size_t hops = 0;
        for (Entity ancestor = parent; ancestor != INVALID_ENTITY && ancestor < slotCount; ancestor = world.m_db.hierarchies[ancestor].parent) {
            if (++hops > slotCount) {
                fail("parent cycle through entity " + text(index));
                return;
            }
        }
    }

    void walkChildList(Entity head, Entity expectedParent, const std::string& label, size_t& visited) {
        Entity prev = INVALID_ENTITY;
        size_t steps = 0;
        for (Entity child = head; child != INVALID_ENTITY; child = world.m_db.nextSibling[child]) {
            if (child >= slotCount || !world.m_alive[child]) {
                fail(label + " lists dead or invalid entity");
                return;
            }
            if (++steps > slotCount) {
                fail(label + " child list loops");
                return;
            }
            if (world.m_db.hierarchies[child].parent != expectedParent) fail(label + " child " + text(child) + " has a different parent");
            if (world.m_prevSibling[child] != prev) fail(label + " child " + text(child) + " has broken prev link");
            prev = child;
            ++visited;
        }
    }

    void checkSiblingLists() {
        size_t visited = 0;
        walkChildList(world.m_firstRoot, INVALID_ENTITY, "root list", visited);
        for (Entity index = 0; index < slotCount; ++index) {
            if (world.m_alive[index]) walkChildList(world.m_db.firstChild[index], index, "children of " + text(index), visited);
        }
        if (visited != flaggedAlive) fail("sibling lists reach " + text(visited) + " of " + text(flaggedAlive) + " entities");
    }

    void checkRegistries() {
        for (const auto& [type, componentPool] : world.m_pools) {
            std::string reason;
            if (!componentPool->validate(world.m_alive, reason)) fail("component pool: " + reason);
        }
        for (const auto& [index, scripts] : world.m_scripts) {
            if (index >= slotCount || !world.m_alive[index]) fail("scripts attached to dead entity " + text(index));
            for (const auto& script : scripts) {
                if (!script) fail("null script on entity " + text(index));
            }
        }
    }

    void checkTags() {
        size_t indexed = 0;
        for (const auto& [tag, bucket] : world.m_tagIndex) {
            if (bucket.empty()) fail("empty tag bucket '" + tag + "'");
            for (Entity entity : bucket) {
                ++indexed;
                if (entity >= slotCount || !world.m_alive[entity] || world.m_tags.get(entity) != tag) fail("tag bucket '" + tag + "' holds stale entity");
            }
        }
        size_t tagged = 0;
        for (Entity index = 0; index < slotCount; ++index) {
            if (world.m_alive[index] && !world.m_tags.get(index).empty()) ++tagged;
            if (!world.m_alive[index] && !world.m_tags.get(index).empty()) fail("dead entity " + text(index) + " keeps a tag");
        }
        if (indexed != tagged) fail("tag index size differs from tagged entity count");
    }

    std::vector<uint8_t> checkSpatialCells() {
        std::vector<uint8_t> inCell(slotCount, 0);
        for (const auto& [key, bucket] : world.m_cells) {
            if (bucket.empty()) fail("empty spatial cell");
            for (Entity entity : bucket) {
                if (entity >= slotCount || entity >= world.m_spatial.size()) {
                    fail("spatial cell holds out-of-range entity");
                    continue;
                }
                if (inCell[entity]) fail("entity " + text(entity) + " appears in several spatial cells");
                inCell[entity] = 1;
                const SpatialEntry& entry = world.m_spatial[entity];
                if (!pendingSpatial[entity] && (entry.state != 1 || entry.cell != key || !world.m_alive[entity])) fail("spatial cell entry for entity " + text(entity) + " is stale");
                if (entry.slot >= bucket.size() || bucket[entry.slot] != entity) fail("spatial slot of entity " + text(entity) + " is wrong");
            }
        }
        return inCell;
    }

    std::vector<uint8_t> checkLargeEntities() {
        std::vector<uint8_t> inLarge(slotCount, 0);
        for (Entity entity : world.m_largeEntities) {
            if (entity >= slotCount || entity >= world.m_spatial.size()) {
                fail("large list holds out-of-range entity");
                continue;
            }
            if (inLarge[entity]) fail("entity " + text(entity) + " appears twice in the large list");
            inLarge[entity] = 1;
            if (!pendingSpatial[entity] && (world.m_spatial[entity].state != 2 || !world.m_alive[entity])) fail("large list entry for entity " + text(entity) + " is stale");
        }
        return inLarge;
    }

    void checkSpatial() {
        if (!world.m_spatialActive) return;
        world.expandSpatialRoots();
        pendingSpatial.assign(slotCount, 0);
        for (Entity entity : world.m_spatialPending) {
            if (entity < slotCount) pendingSpatial[entity] = 1;
        }
        const std::vector<uint8_t> inCell = checkSpatialCells();
        const std::vector<uint8_t> inLarge = checkLargeEntities();
        for (Entity entity = 0; entity < slotCount && entity < world.m_spatial.size(); ++entity) {
            if (pendingSpatial[entity]) continue;
            const uint8_t state = world.m_spatial[entity].state;
            if (state == 1 && !inCell[entity]) fail("entity " + text(entity) + " has cell state but is not in a cell");
            if (state == 2 && !inLarge[entity]) fail("entity " + text(entity) + " has large state but is not listed");
            if (!world.m_alive[entity] && state != 0) fail("dead entity " + text(entity) + " keeps spatial state");
        }
    }

    bool matrixDiffers(const glm::mat4& expected, const glm::mat4& cached) const {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(expected[column][row] - cached[column][row]) > kCacheToleranceBase * (1.0f + std::abs(expected[column][row]))) return true;
            }
        }
        return false;
    }

    void checkWorldCache() {
        if (world.m_worldCache.empty() || world.m_worldCache.size() != slotCount || world.m_worldDirty.size() != slotCount) return;
        for (Entity index = 0; index < slotCount; ++index) {
            if (!world.m_alive[index] || world.m_worldDirty[index]) continue;
            const Entity parent = world.m_db.hierarchies[index].parent;
            if (parent != INVALID_ENTITY && parent < slotCount && world.m_worldDirty[parent]) fail("clean world cache under dirty parent at entity " + text(index));
            if (!matrixDiffers(world.worldNoCache(index), world.m_worldCache[index])) continue;
            const std::string parentState = parent == INVALID_ENTITY ? std::string("none") : text(parent) + (world.m_worldDirty[parent] ? " dirty" : " clean");
            fail("stale world cache for entity " + text(index) + " parent " + parentState + " animOffset " + text(world.m_animOffset) + " animCount " + text(world.m_animBase.size()));
        }
    }

    void checkActiveCamera() {
        const EntityHandle camera = world.m_activeCamera;
        if (!camera.isNull() && camera.index < slotCount && world.m_generation[camera.index] == camera.generation && !world.m_alive[camera.index]) fail("active camera points at a dead entity");
    }

    std::vector<std::string> run() {
        world.syncAnimationCache();
        if (!checkArraySizes()) return std::move(problems);
        checkAliveCountAndFreeList();
        checkEntities();
        checkSiblingLists();
        checkRegistries();
        checkTags();
        checkSpatial();
        checkWorldCache();
        checkActiveCamera();
        return std::move(problems);
    }
};

std::vector<std::string> World::validate() { return Validator(*this).run(); }
