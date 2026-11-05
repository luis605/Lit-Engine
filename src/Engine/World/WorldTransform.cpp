module;

#include <string>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

module Engine.World;

import Engine.Render.entity;
import Engine.Render.component;
import Engine.Render.scenedatabase;
import Engine.camera;
import Engine.glm;

#include "Engine/World/WorldInternal.hpp"

namespace {

constexpr float kAxisEpsilon = 1.0e-8f;
constexpr float kLookParallelEpsilon = 1.0e-6f;
constexpr float kOrbitAmplitude = 2.0f;

glm::vec3 axisOf(const glm::mat4& matrix, int column) {
    const glm::vec3 axis(matrix[column]);
    const float length = glm::length(axis);
    if (length > kAxisEpsilon) return axis / length;
    return glm::vec3(column == 0 ? 1.0f : 0.0f, column == 1 ? 1.0f : 0.0f, column == 2 ? 1.0f : 0.0f);
}

const glm::mat4 kDegenerateMatrix(0.0f);

}

void World::rebuildPositions() {
    m_position.resize(m_db.transforms.size());
    for (size_t slot = 0; slot < m_position.size(); ++slot) syncPosition(static_cast<Entity>(slot));
}

void World::touchTransform(Entity index) {
    m_db.transformDirty.mark(index);
    invalidateWorld(index);
    markSpatialSubtree(index);
}

void World::invalidateWorld(Entity index) const {
    if (m_worldCache.empty() || index >= m_worldDirty.size()) return;
    m_worldStack.clear();
    m_worldStack.push_back(index);
    while (!m_worldStack.empty()) {
        const Entity current = m_worldStack.back();
        m_worldStack.pop_back();
        if (current >= m_worldDirty.size() || m_worldDirty[current]) continue;
        m_worldDirty[current] = 1;
        for (Entity child = m_db.firstChild[current]; child != INVALID_ENTITY; child = m_db.nextSibling[child]) m_worldStack.push_back(child);
    }
}

void World::setPositions(std::span<const std::pair<EntityHandle, glm::vec3>> updates) {
    constexpr size_t kBulkThreshold = 4096;
    const bool bulk = updates.size() > kBulkThreshold && updates.size() * 2 > m_db.transforms.size();
    for (const auto& [entity, position] : updates) {
        if (!valid(entity)) continue;
        m_db.transforms[entity.index].setPos(position);
        m_position[entity.index] = position;
        if (!bulk) m_db.transformDirty.mark(entity.index);
        invalidateWorld(entity.index);
        markSpatialSubtree(entity.index);
    }
    if (bulk) m_db.markTransformsDirty();
}

void World::setLocalMatrix(EntityHandle entity, const glm::mat4& local) {
    if (!valid(entity)) return;
    m_db.transforms[entity.index].localMatrix = local;
    syncPosition(entity.index);
    touchTransform(entity.index);
}

void World::setPosition(EntityHandle entity, const glm::vec3& position) {
    if (!valid(entity)) return;
    m_db.transforms[entity.index].setPos(position);
    m_position[entity.index] = position;
    touchTransform(entity.index);
}

void World::setRotation(EntityHandle entity, const glm::quat& rotation) {
    if (!valid(entity)) return;
    m_db.transforms[entity.index].setRot(rotation);
    touchTransform(entity.index);
}

void World::setScale(EntityHandle entity, const glm::vec3& scale) {
    if (!valid(entity)) return;
    m_db.transforms[entity.index].setScale(scale);
    touchTransform(entity.index);
}

void World::translate(EntityHandle entity, const glm::vec3& delta) {
    if (!valid(entity)) return;
    TransformComponent& transform = m_db.transforms[entity.index];
    transform.setPos(transform.getPos() + delta);
    syncPosition(entity.index);
    touchTransform(entity.index);
}

void World::setWorldMatrix(EntityHandle entity, const glm::mat4& world) {
    if (!valid(entity)) return;
    const EntityHandle parent = getParent(entity);
    const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : getWorldMatrix(parent);
    setLocalMatrix(entity, glm::inverse(parentWorld) * world);
}

const glm::mat4& World::getLocalMatrix(EntityHandle entity) const {
    return valid(entity) ? m_db.transforms[entity.index].localMatrix : kDegenerateMatrix;
}

glm::vec3 World::orbitOffset(float time, uint32_t animIndex) {
    const float phase = static_cast<float>(animIndex);
    return glm::vec3(std::sin(time * 1.3f + phase * 1.0f), std::cos(time * 0.9f + phase * 0.5f), std::sin(time * 0.7f + phase * 0.25f)) * kOrbitAmplitude;
}

void World::setAnimation(uint32_t firstEntity, std::vector<glm::vec3> basePositions) {
    if (!m_animBase.empty()) {
        const size_t oldEnd = std::min<size_t>(m_animBase.size() + m_animOffset, m_alive.size());
        for (size_t slot = m_animOffset; slot < oldEnd; ++slot) {
            if (!m_alive[slot]) continue;
            invalidateWorld(static_cast<Entity>(slot));
            queueSpatial(static_cast<Entity>(slot));
        }
    }
    m_animOffset = firstEntity;
    m_animBase = std::move(basePositions);
    m_animCacheTime = std::numeric_limits<float>::quiet_NaN();
}

bool World::isAnimated(Entity index) const {
    return index >= m_animOffset && index - m_animOffset < m_animBase.size();
}

glm::mat4 World::effectiveLocal(Entity index) const {
    glm::mat4 local = m_db.transforms[index].localMatrix;
    if (isAnimated(index)) {
        const uint32_t animIndex = index - m_animOffset;
        local[3] = glm::vec4(m_animBase[animIndex] + orbitOffset(m_animTime, animIndex), 1.0f);
    }
    return local;
}

void World::syncAnimationCache() const {
    if (m_animBase.empty() || m_animCacheTime == m_animTime) return;
    m_animCacheTime = m_animTime;
    if (m_worldCache.empty()) return;
    const size_t end = std::min<size_t>(m_animBase.size() + m_animOffset, m_alive.size());
    for (size_t slot = m_animOffset; slot < end; ++slot) {
        if (m_alive[slot]) invalidateWorld(static_cast<Entity>(slot));
    }
}

glm::mat4 World::worldNoCache(Entity index) const {
    glm::mat4 matrix = effectiveLocal(index);
    for (Entity ancestor = m_db.hierarchies[index].parent; ancestor != INVALID_ENTITY && m_alive[ancestor]; ancestor = m_db.hierarchies[ancestor].parent) {
        matrix = effectiveLocal(ancestor) * matrix;
    }
    return matrix;
}

glm::mat4 World::getWorldMatrix(EntityHandle entity) const {
    if (!valid(entity)) return glm::mat4(1.0f);
    syncAnimationCache();
    if (m_worldCache.size() < m_alive.size()) {
        m_worldCache.resize(m_alive.size(), glm::mat4(1.0f));
        m_worldDirty.resize(m_alive.size(), 1);
    }
    m_worldChain.clear();
    Entity current = entity.index;
    while (current != INVALID_ENTITY && m_worldDirty[current]) {
        m_worldChain.push_back(current);
        current = m_db.hierarchies[current].parent;
    }
    glm::mat4 accumulated = current == INVALID_ENTITY ? glm::mat4(1.0f) : m_worldCache[current];
    for (size_t position = m_worldChain.size(); position-- > 0;) {
        const Entity chained = m_worldChain[position];
        accumulated = accumulated * effectiveLocal(chained);
        m_worldCache[chained] = accumulated;
        m_worldDirty[chained] = 0;
    }
    return m_worldCache[entity.index];
}

glm::quat World::getRotation(EntityHandle entity) const {
    return valid(entity) ? m_db.transforms[entity.index].getRot() : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

glm::vec3 World::getScale(EntityHandle entity) const {
    return valid(entity) ? m_db.transforms[entity.index].getScale() : glm::vec3(0.0f);
}

glm::vec3 World::getWorldPosition(EntityHandle entity) const { return glm::vec3(getWorldMatrix(entity)[3]); }

glm::quat World::getWorldRotation(EntityHandle entity) const {
    if (!valid(entity)) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::mat4 matrix = getWorldMatrix(entity);
    return glm::normalize(glm::quat_cast(glm::mat3(axisOf(matrix, 0), axisOf(matrix, 1), axisOf(matrix, 2))));
}

glm::vec3 World::forward(EntityHandle entity) const { return valid(entity) ? -axisOf(getWorldMatrix(entity), 2) : glm::vec3(0.0f, 0.0f, -1.0f); }

glm::vec3 World::right(EntityHandle entity) const { return valid(entity) ? axisOf(getWorldMatrix(entity), 0) : glm::vec3(1.0f, 0.0f, 0.0f); }

glm::vec3 World::up(EntityHandle entity) const { return valid(entity) ? axisOf(getWorldMatrix(entity), 1) : glm::vec3(0.0f, 1.0f, 0.0f); }

void World::setWorldPosition(EntityHandle entity, const glm::vec3& position) {
    if (!valid(entity)) return;
    glm::mat4 matrix = getWorldMatrix(entity);
    matrix[3] = glm::vec4(position, 1.0f);
    setWorldMatrix(entity, matrix);
}

void World::setWorldRotation(EntityHandle entity, const glm::quat& rotation) {
    if (!valid(entity)) return;
    const glm::mat4 matrix = getWorldMatrix(entity);
    const glm::vec3 scale(glm::length(glm::vec3(matrix[0])), glm::length(glm::vec3(matrix[1])), glm::length(glm::vec3(matrix[2])));
    setWorldMatrix(entity, composeTransform(glm::vec3(matrix[3]), glm::normalize(rotation), scale));
}

void World::rotate(EntityHandle entity, const glm::vec3& axis, float radians) {
    if (!valid(entity) || glm::length(axis) < kAxisEpsilon) return;
    setRotation(entity, glm::angleAxis(radians, glm::normalize(axis)) * getRotation(entity));
}

void World::lookAt(EntityHandle entity, const glm::vec3& target, const glm::vec3& worldUp) {
    if (!valid(entity)) return;
    const glm::vec3 delta = target - getWorldPosition(entity);
    if (glm::length(delta) < kAxisEpsilon) return;
    const glm::vec3 forwardAxis = glm::normalize(delta);
    glm::vec3 rightAxis = glm::cross(forwardAxis, worldUp);
    if (glm::length(rightAxis) < kLookParallelEpsilon) rightAxis = glm::cross(forwardAxis, glm::vec3(0.0f, 0.0f, 1.0f));
    rightAxis = glm::normalize(rightAxis);
    const glm::vec3 upAxis = glm::cross(rightAxis, forwardAxis);
    setWorldRotation(entity, glm::quat_cast(glm::mat3(rightAxis, upAxis, -forwardAxis)));
}

void World::syncCamera() {
    if (!valid(m_activeCamera)) return;
    const CameraComponent* cameraComponent = get<CameraComponent>(m_activeCamera);
    if (!cameraComponent) return;
    const glm::mat4 world = getWorldMatrix(m_activeCamera);
    const glm::vec3 forwardAxis = -axisOf(world, 2);
    m_camera.setPos(glm::vec3(world[3]));
    m_camera.setOrientation(glm::degrees(std::atan2(forwardAxis.z, forwardAxis.x)), glm::degrees(std::asin(glm::clamp(forwardAxis.y, -1.0f, 1.0f))));
    m_camera.setFov(cameraComponent->fov);
    m_camera.setNearPlane(cameraComponent->nearPlane);
    m_camera.setFarPlane(cameraComponent->farPlane);
}
