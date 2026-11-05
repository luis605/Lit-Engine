module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Engine/Log/Log.hpp"
#include "Editor/Application/ApplicationInternal.hpp"

module Editor.application;

import Engine.engine;
import Engine.mesh;
import Engine.World;
import Engine.Portal;
import Engine.History;
import Engine.Physics;
import Engine.Animation;
import Engine.Render.component;
import Engine.Render.entity;
import Engine.Render.material;
import Engine.Render.settings;
import Engine.Render.custom;
import Engine.camera;
import Engine.Overlay;
import Engine.Observation;
import Engine.TestScenes;
import Engine.Params;
import Engine.input;
import Engine.GizmoMath;
import Engine.glm;
import Editor.context;
import Editor.commands;
import Editor.viewport;
import Editor.gizmo;
import Editor.hierarchy;
import Editor.inspector;
import Editor.palette;
import Editor.hud;
import Editor.look;
import Editor.autopilot;

namespace {

constexpr size_t kSiblingScanLimit = 2000000;
constexpr size_t kFrameSphereLimit = 4096;
constexpr float kDefaultFramePivotDistance = 20.0f;
constexpr float kOrthoPivotDistance = 10.0f;
constexpr float kMinViewDistance = 5.0f;
constexpr float kMinOrthoDistance = 1.0f;
constexpr float kBookmarkFlightSeconds = 0.35f;
constexpr float kMaxViewPitch = 89.0f;
constexpr float kViewDirectionBias = 1e-4f;
constexpr float kSurfaceSpawnLift = 0.5f;
constexpr float kDefaultBoundsRadius = 0.5f;

EntityHandle previousSibling(World& world, EntityHandle current) {
    const EntityHandle parent = world.getParent(current);
    EntityHandle probe = parent.isNull() ? world.firstRoot() : world.firstChild(parent);
    EntityHandle previous;
    size_t scanned = 0;
    while (!probe.isNull() && probe != current && scanned++ < kSiblingScanLimit) {
        previous = probe;
        probe = world.nextSibling(probe);
    }
    return previous;
}

EntityHandle nextSiblingWrapping(World& world, EntityHandle current) {
    const EntityHandle next = world.nextSibling(current);
    if (!next.isNull()) return next;
    const EntityHandle parent = world.getParent(current);
    return parent.isNull() ? world.firstRoot() : world.firstChild(parent);
}

glm::vec3 viewPivot(EditorContext& ctx, const Camera& camera, float fallbackDistance) {
    if (!ctx.selection.empty()) return ctx.world.getWorldPosition(ctx.primary());
    if (ctx.cursorOnSurface) return ctx.cursor;
    return camera.getPosition() + cameraForward(camera) * fallbackDistance;
}

float orthoHeightForDistance(const Camera& camera, float distance) {
    return 2.0f * distance * std::tan(glm::radians(camera.getFov()) * 0.5f);
}

void configureLight(EditorContext& ctx, EntityHandle entity, const std::string& kind) {
    if (kind == "Point light") {
        ctx.world.setComponentFromText(entity, "Light", "1 1 0.95 0.9 40 25 1 20 30");
    } else if (kind == "Spot light") {
        ctx.world.setComponentFromText(entity, "Light", "2 1 1 1 80 40 1 18 28");
        ctx.world.setRotation(entity, glm::angleAxis(glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    } else if (kind == "Sun light") {
        ctx.world.setComponentFromText(entity, "Light", "0 1 0.96 0.9 3 100 1 20 30");
        ctx.world.setRotation(entity, glm::angleAxis(glm::radians(-50.0f), glm::vec3(1.0f, 0.0f, 0.0f)));
    } else if (kind == "Camera") {
        ctx.world.setComponentFromText(entity, "Camera", "60 0.1 2000");
    }
}

}

bool Application::hasSelection() const { return !m_ctx->selection.empty(); }

std::function<bool()> Application::selectionRequired() const {
    return [this] { return hasSelection(); };
}

void Application::duplicateSelection(bool grab) {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    std::vector<EntityHandle> copies;
    copies.reserve(top.size());
    ctx.history->beginGroup(top.size() > 1 ? std::format("Duplicate x{}", top.size()) : "Duplicate");
    for (EntityHandle entity : top) {
        const Prefab prefab = ctx.world.capture(entity);
        copies.push_back(ctx.history->instantiate(prefab, ctx.world.getParent(entity)));
    }
    ctx.history->endGroup();
    ctx.selection.setAll(copies);
    if (grab) m_tool.begin(ctx, ModalKind::Move);
}

void Application::deleteSelection() {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    ctx.history->beginGroup(top.size() > 1 ? std::format("Delete x{}", top.size()) : "Delete");
    for (EntityHandle entity : top) ctx.history->destroy(entity);
    ctx.history->endGroup();
    ctx.selection.clear();
    ctx.toast(std::format("Deleted {} (Ctrl+Z to restore)", top.size()));
}

void Application::copySelection() {
    EditorContext& ctx = *m_ctx;
    ctx.clipboard.clear();
    for (EntityHandle entity : ctx.topLevelSelection()) ctx.clipboard.push_back(ctx.world.capture(entity));
    if (!ctx.clipboard.empty()) ctx.toast(std::format("Copied {}", ctx.clipboard.size()));
}

void Application::paste() {
    EditorContext& ctx = *m_ctx;
    glm::vec3 center(0.0f);
    size_t pasteable = 0;
    for (const Prefab& prefab : ctx.clipboard) {
        if (prefab.nodes.empty()) continue;
        center += glm::vec3(prefab.nodes.front().local[3]);
        ++pasteable;
    }
    if (pasteable == 0) return;
    center /= static_cast<float>(pasteable);
    const glm::vec3 offset = spawnPoint(ctx).position - center;
    std::vector<EntityHandle> created;
    created.reserve(pasteable);
    ctx.history->beginGroup("Paste");
    for (Prefab prefab : ctx.clipboard) {
        if (prefab.nodes.empty()) continue;
        prefab.nodes.front().local[3] += glm::vec4(offset, 0.0f);
        created.push_back(ctx.history->instantiate(prefab));
    }
    ctx.history->endGroup();
    ctx.selection.setAll(created);
}

EntityHandle Application::createPrimitive(const std::string& kind) {
    EditorContext& ctx = *m_ctx;
    EntityDesc desc;
    const SpawnPoint spawn = spawnPoint(ctx);
    desc.position = spawn.position + glm::vec3(0.0f, spawn.onSurface ? kSurfaceSpawnLift : 0.0f, 0.0f);
    desc.name = kind;
    if (kind == "Cube") {
        desc.mesh = ctx.cubeMesh;
    } else if (kind == "Sphere") {
        desc.mesh = ctx.sphereMesh;
    } else if (kind == "Empty" || kind.find("light") != std::string::npos || kind == "Camera") {
        desc.mesh = NO_MESH;
    } else {
        desc.mesh = m_engine.loadMesh(kind);
    }
    const EntityHandle entity = ctx.history->create(desc);
    configureLight(ctx, entity, kind);
    ctx.selection.set(entity);
    m_outliner.reveal(ctx, entity);
    return entity;
}

void Application::groupSelection() {
    EditorContext& ctx = *m_ctx;
    const auto top = ctx.topLevelSelection();
    if (top.empty()) return;
    glm::vec3 center(0.0f);
    for (EntityHandle entity : top) center += ctx.world.getWorldPosition(entity);
    center /= static_cast<float>(top.size());
    ctx.history->beginGroup("Group");
    EntityDesc desc;
    desc.name = "Group";
    desc.position = center;
    desc.parent = ctx.world.getParent(top.front());
    const EntityHandle group = ctx.history->create(desc);
    for (EntityHandle entity : top) ctx.history->setParent(entity, group);
    ctx.history->endGroup();
    ctx.selection.set(group);
    m_outliner.reveal(ctx, group);
}

void Application::parentToActive() {
    EditorContext& ctx = *m_ctx;
    const EntityHandle active = ctx.primary();
    if (ctx.selection.size() < 2) {
        ctx.toast("Select children first, then the new parent last", ToastKind::Warning);
        return;
    }
    ctx.history->beginGroup("Parent");
    for (EntityHandle entity : ctx.selection.items()) {
        if (entity != active && !ctx.world.isDescendantOf(active, entity)) ctx.history->setParent(entity, active);
    }
    ctx.history->endGroup();
    m_outliner.reveal(ctx, active);
}

void Application::walk(int direction) {
    EditorContext& ctx = *m_ctx;
    World& world = ctx.world;
    const EntityHandle current = ctx.primary();
    if (!world.isAlive(current)) {
        ctx.selection.set(world.firstRoot());
        return;
    }
    EntityHandle next;
    if (direction == kWalkParent) next = world.getParent(current);
    else if (direction == kWalkChild) next = world.firstChild(current);
    else if (direction == kWalkNext) next = nextSiblingWrapping(world, current);
    else next = previousSibling(world, current);
    if (!world.isAlive(next)) return;
    ctx.selection.set(next);
    m_outliner.reveal(ctx, next);
}

void Application::frameSelection() {
    EditorContext& ctx = *m_ctx;
    if (ctx.selection.empty()) {
        frameAll();
        return;
    }
    std::vector<glm::vec4> spheres;
    for (EntityHandle entity : ctx.selection.items()) {
        if (spheres.size() > kFrameSphereLimit) break;
        const glm::vec4 bounds = ctx.world.getWorldBounds(entity);
        spheres.push_back(bounds.w > 0.0f ? bounds : glm::vec4(ctx.world.getWorldPosition(entity), kDefaultBoundsRadius));
    }
    flyTo(ctx, gizmo::enclosingSphere(spheres));
}

void Application::frameAll() {
    if (const std::optional<glm::vec4> bounds = m_ctx->world.sceneBounds()) flyTo(*m_ctx, *bounds);
}

void Application::setView(const glm::vec3& direction) {
    EditorContext& ctx = *m_ctx;
    Camera& camera = ctx.world.camera();
    const glm::vec3 pivot = viewPivot(ctx, camera, kDefaultFramePivotDistance);
    const float distance = std::max(glm::length(camera.getPosition() - pivot), kMinViewDistance);
    const glm::vec3 viewDirection = glm::normalize(direction + glm::vec3(0.0f, 0.0f, kViewDirectionBias));
    CameraFlight& flight = ctx.flight;
    flight.active = true;
    flight.t = 0.0f;
    flight.from = camera.getPosition();
    flight.to = pivot - viewDirection * distance;
    flight.fromYaw = camera.getYaw();
    flight.fromPitch = camera.getPitch();
    flight.toYaw = glm::degrees(std::atan2(viewDirection.z, viewDirection.x));
    flight.toPitch = std::clamp(glm::degrees(std::asin(std::clamp(viewDirection.y, -1.0f, 1.0f))), -kMaxViewPitch, kMaxViewPitch);
    flight.fromOrtho = camera.isOrthographic() ? camera.getOrthoHeight() : 0.0f;
    flight.toOrtho = orthoHeightForDistance(camera, distance);
    ctx.autoOrtho = true;
}

void Application::toggleOrthographic() {
    EditorContext& ctx = *m_ctx;
    Camera& camera = ctx.world.camera();
    ctx.flight.active = false;
    ctx.autoOrtho = false;
    if (camera.isOrthographic()) {
        camera.setOrthographic(false);
        return;
    }
    const glm::vec3 pivot = viewPivot(ctx, camera, kOrthoPivotDistance);
    const float distance = std::max(glm::dot(pivot - camera.getPosition(), cameraForward(camera)), kMinOrthoDistance);
    camera.setOrthoHeight(orthoHeightForDistance(camera, distance));
    camera.setOrthographic(true);
}

void Application::storeSelectionSet(int slot) {
    m_ctx->selectionSets[slot] = m_ctx->selection.items();
    m_ctx->toast(std::format("Selection set {} stored ({} entities)", slot, m_ctx->selection.size()), ToastKind::Success);
}

void Application::recallSelectionSet(int slot) {
    std::vector<EntityHandle> alive;
    for (EntityHandle entity : m_ctx->selectionSets[slot]) {
        const EntityHandle resolved = m_ctx->history->resolve(entity);
        if (m_ctx->world.isAlive(resolved)) alive.push_back(resolved);
    }
    if (alive.empty()) {
        m_ctx->toast(std::format("Selection set {} is empty. Ctrl+{} stores one", slot, slot), ToastKind::Warning);
        return;
    }
    m_ctx->selection.setAll(alive);
    if (ImGui::GetIO().KeyShift) frameSelection();
}

void Application::storeBookmark(int slot) {
    const Camera& camera = m_ctx->world.camera();
    m_ctx->bookmarks[slot] = {camera.getPosition(), camera.getYaw(), camera.getPitch(), true};
    m_ctx->toast(std::format("Camera view {} stored", slot), ToastKind::Success);
}

void Application::recallBookmark(int slot) {
    const CameraBookmark& bookmark = m_ctx->bookmarks[slot];
    if (!bookmark.valid) {
        m_ctx->toast(std::format("No camera view {}. Ctrl+Alt+{} stores one", slot, slot), ToastKind::Warning);
        return;
    }
    Camera& camera = m_ctx->world.camera();
    CameraFlight& flight = m_ctx->flight;
    flight.active = true;
    flight.t = 0.0f;
    flight.duration = kBookmarkFlightSeconds;
    flight.from = camera.getPosition();
    flight.to = bookmark.position;
    flight.fromYaw = camera.getYaw();
    flight.toYaw = bookmark.yaw;
    flight.fromPitch = camera.getPitch();
    flight.toPitch = bookmark.pitch;
    flight.fromOrtho = flight.toOrtho = 0.0f;
    camera.setOrthographic(false);
}

void Application::selectSimilar(bool byMaterial) {
    EditorContext& ctx = *m_ctx;
    const EntityHandle primary = ctx.primary();
    if (!ctx.world.isAlive(primary)) return;
    const uint32_t mesh = ctx.world.getMesh(primary);
    const uint32_t material = ctx.world.getRenderable(primary).material_uuid;
    std::vector<EntityHandle> found;
    ctx.world.forEach([&](EntityHandle entity) {
        if (found.size() >= kSelectionLimit) return;
        const bool matches = byMaterial ? ctx.world.getRenderable(entity).material_uuid == material : ctx.world.getMesh(entity) == mesh;
        if (matches) found.push_back(entity);
    });
    ctx.selection.setAll(found);
    ctx.selection.remove(primary);
    ctx.selection.add(primary);
    ctx.toast(std::format("Selected {} with the same {}{}", found.size(), byMaterial ? "material" : "mesh", found.size() >= kSelectionLimit ? " (capped)" : ""));
}

void Application::setSelectionMaterial(uint32_t materialId) {
    for (EntityHandle entity : m_ctx->topLevelSelection()) {
        const RenderableComponent renderable = m_ctx->world.getRenderable(entity);
        m_ctx->history->setRender(entity, m_ctx->world.getMesh(entity), materialId, renderable.shaderId, renderable.alpha);
    }
}

void Application::assignMaterialFromPrompt(const std::string& picked) {
    const size_t hash = picked.rfind('#');
    if (hash == std::string::npos) return;
    const uint32_t materialId = static_cast<uint32_t>(std::atoi(picked.c_str() + hash + 1));
    if (materialId >= m_engine.materialCount()) return;
    m_ctx->history->beginGroup("Assign material");
    setSelectionMaterial(materialId);
    m_ctx->history->endGroup();
}

void Application::moveSelectionToCursor() {
    const auto top = m_ctx->topLevelSelection();
    if (top.empty()) return;
    glm::vec3 center(0.0f);
    for (EntityHandle entity : top) center += m_ctx->world.getWorldPosition(entity);
    center /= static_cast<float>(top.size());
    const glm::vec3 delta = m_ctx->cursor - center;
    m_ctx->history->beginGroup("Move to cursor");
    for (EntityHandle entity : top) {
        const EntityHandle parent = m_ctx->world.getParent(entity);
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : m_ctx->world.getWorldMatrix(parent);
        m_ctx->history->setLocalMatrix(entity, glm::inverse(parentWorld) * glm::translate(glm::mat4(1.0f), delta) * m_ctx->world.getWorldMatrix(entity));
    }
    m_ctx->history->endGroup();
}

void Application::dropSelectionToGround() {
    constexpr float kBoundsGroundFactor = 0.58f;
    m_ctx->history->beginGroup("Drop to ground");
    for (EntityHandle entity : m_ctx->topLevelSelection()) {
        const glm::vec4 bounds = m_ctx->world.getWorldBounds(entity);
        const glm::vec3 position = m_ctx->world.getWorldPosition(entity);
        const float lift = bounds.w > 0.0f ? position.y - bounds.y + bounds.w * kBoundsGroundFactor : 0.0f;
        const EntityHandle parent = m_ctx->world.getParent(entity);
        const glm::mat4 parentWorld = parent.isNull() ? glm::mat4(1.0f) : m_ctx->world.getWorldMatrix(parent);
        glm::mat4 worldMatrix = m_ctx->world.getWorldMatrix(entity);
        worldMatrix[3].y = lift;
        m_ctx->history->setLocalMatrix(entity, glm::inverse(parentWorld) * worldMatrix);
    }
    m_ctx->history->endGroup();
}

void Application::revealHiddenEntities() {
    World& world = m_ctx->world;
    auto hidden = std::make_shared<std::vector<EntityHandle>>();
    world.forEach([&](EntityHandle entity) {
        if (!world.isVisible(entity)) hidden->push_back(entity);
    });
    const size_t shown = hidden->size();
    if (shown > 0) {
        auto apply = [&world, hidden](bool visible) {
            for (EntityHandle entity : *hidden) world.setVisible(entity, visible);
        };
        apply(true);
        m_ctx->history->record("Show all", [apply] { apply(true); }, [apply] { apply(false); });
    }
    m_ctx->toast(std::format("Revealed {}", shown));
}
