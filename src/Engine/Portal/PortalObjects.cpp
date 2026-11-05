module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Engine.Portal;

import Engine.glm;
import Engine.camera;
import Engine.World;
import Engine.Render.entity;
import Engine.Render.component;
import Engine.Portal.view;

#include "PortalInternal.hpp"

namespace {
constexpr float kNoStraddleDistance = 1.0e30f;
}

void PortalSystem::track(World& world, EntityHandle entity) {
    const bool alreadyTracked = std::ranges::any_of(m_tracked, [&](const Tracked& tracked) { return tracked.entity == entity; });
    if (alreadyTracked) return;
    Tracked tracked;
    tracked.entity = entity;
    tracked.previous = world.getWorldPosition(entity);
    tracked.cell = cellAt(tracked.previous);
    m_tracked.push_back(tracked);
}

void PortalSystem::untrack(World& world, EntityHandle entity) {
    const auto found = std::ranges::find_if(m_tracked, [&](const Tracked& tracked) { return tracked.entity == entity; });
    if (found == m_tracked.end()) return;
    if (world.isAlive(found->ghost)) world.destroy(found->ghost);
    m_tracked.erase(found);
}

EntityHandle PortalSystem::ghostOf(EntityHandle entity) const {
    for (const Tracked& tracked : m_tracked) {
        if (tracked.entity == entity) return tracked.ghost;
    }
    return NULL_ENTITY;
}

int PortalSystem::objectCell(EntityHandle entity) const {
    for (const Tracked& tracked : m_tracked) {
        if (tracked.entity == entity) return tracked.cell;
    }
    return -1;
}

bool PortalSystem::isGhost(EntityHandle entity) const {
    if (entity == NULL_ENTITY) return false;
    return std::ranges::any_of(m_tracked, [&](const Tracked& tracked) { return tracked.ghost == entity; });
}

namespace {
int findStraddledPortal(const std::vector<Portal>& portals, const std::vector<int>& candidates, const glm::vec3& position, float radius) {
    int straddled = -1;
    float best = kNoStraddleDistance;
    for (int id : candidates) {
        float side = 0.0f;
        const float distance = rectDistance(portals[static_cast<size_t>(id)], position, side);
        if (distance < radius && side >= -radius && distance < best) {
            best = distance;
            straddled = id;
        }
    }
    return straddled;
}
}

void PortalSystem::updateGhost(World& world, Tracked& tracked, int straddledPortal) {
    if (straddledPortal < 0) {
        if (world.isAlive(tracked.ghost)) world.setVisible(tracked.ghost, false);
        return;
    }
    if (!world.isAlive(tracked.ghost)) {
        const RenderableComponent& renderable = world.getRenderable(tracked.entity);
        EntityDesc desc;
        desc.name = world.getName(tracked.entity) + " (portal twin)";
        desc.mesh = world.getMesh(tracked.entity);
        desc.material = renderable.material_uuid;
        desc.shader = renderable.shaderId;
        desc.alpha = renderable.alpha;
        tracked.ghost = world.create(desc);
    }
    world.setWorldMatrix(tracked.ghost, m_portals[static_cast<size_t>(straddledPortal)].transform * world.getWorldMatrix(tracked.entity));
    world.setVisible(tracked.ghost, world.isVisible(tracked.entity));
}

void PortalSystem::updateObjects(World& world) {
    for (size_t index = 0; index < m_tracked.size();) {
        Tracked& tracked = m_tracked[index];
        if (!world.isAlive(tracked.entity)) {
            if (world.isAlive(tracked.ghost)) world.destroy(tracked.ghost);
            m_tracked.erase(m_tracked.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        glm::vec3 position = world.getWorldPosition(tracked.entity);
        if (tracked.cell < 0) tracked.cell = std::max(cellAt(position), 0);
        glm::vec3 moved = position;
        int cell = tracked.cell;
        const int crossed = stepPoint(moved, cell, tracked.previous);
        if (crossed >= 0) {
            const glm::mat4 transform = m_portals[static_cast<size_t>(crossed)].transform;
            world.setWorldMatrix(tracked.entity, transform * world.getWorldMatrix(tracked.entity));
            position = world.getWorldPosition(tracked.entity);
            tracked.cell = cell;
            if (m_crossHook) m_crossHook(world, tracked.entity, transform);
        }
        tracked.previous = position;
        tracked.cell = resolveCell(tracked.cell, position);
        const float radius = world.getWorldBounds(tracked.entity).w;
        const int straddled = findStraddledPortal(m_portals, portalsOf(tracked.cell), position, radius);
        updateGhost(world, tracked, straddled);
        ++index;
    }
}
