module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include "Engine/Log/Log.hpp"

module Editor.inspector;

Inspector::Inspector() = default;
Inspector::~Inspector() = default;

void Inspector::update(Engine& engine, bool pickingEnabled) {
    World& world = engine.world();
    if (!m_history) m_history = std::make_unique<History>(world);
    if (!world.isAlive(m_selected)) {
        m_selected = NULL_ENTITY;
        m_reparenting = false;
    }
    const bool gizmoConsumed = m_gizmo.update(engine, *m_history, m_selected, pickingEnabled);
    if (pickingEnabled && !gizmoConsumed) handleClick(engine);
    m_hierarchy.update(engine, m_selected);
    handleKeys(engine);
    draw(engine);
}

void Inspector::handleClick(Engine& engine) {
    if (!InputManager::IsMouseButtonPressed(GLFW_MOUSE_BUTTON_LEFT)) return;
    const glm::vec2 mouse = InputManager::GetMousePosition();
    const auto hit = engine.pick(mouse.x, mouse.y);
    World& world = engine.world();
    if (m_reparenting && world.isAlive(m_selected)) {
        m_history->setParent(m_selected, hit ? hit->entity : NULL_ENTITY);
        m_reparenting = false;
        return;
    }
    m_selected = hit ? hit->entity : NULL_ENTITY;
}

void Inspector::handleKeys(Engine& engine) {
    World& world = engine.world();
    const bool ctrl = InputManager::IsKeyHeld(GLFW_KEY_LEFT_CONTROL) || InputManager::IsKeyHeld(GLFW_KEY_RIGHT_CONTROL);
    const bool shift = InputManager::IsKeyHeld(GLFW_KEY_LEFT_SHIFT) || InputManager::IsKeyHeld(GLFW_KEY_RIGHT_SHIFT);
    if (ctrl && InputManager::IsKeyPressed(GLFW_KEY_Z)) {
        if (shift) {
            m_history->redo();
        } else {
            m_history->undo();
        }
        return;
    }
    if (ctrl && InputManager::IsKeyPressed(GLFW_KEY_Y)) {
        m_history->redo();
        return;
    }
    if (!world.isAlive(m_selected)) return;

    if (InputManager::IsKeyPressed(GLFW_KEY_H)) m_history->setVisible(m_selected, !world.isVisible(m_selected));
    if (InputManager::IsKeyPressed(GLFW_KEY_DELETE)) {
        m_history->destroy(m_selected);
        m_selected = NULL_ENTITY;
        return;
    }
    if (InputManager::IsKeyPressed(GLFW_KEY_C)) {
        Prefab prefab = world.capture(m_selected);
        if (!prefab.nodes.empty()) prefab.nodes.front().local = glm::translate(prefab.nodes.front().local, glm::vec3(2.0f, 0.0f, 0.0f));
        m_selected = m_history->instantiate(prefab, world.getParent(m_selected));
    }
    if (InputManager::IsKeyPressed(GLFW_KEY_G)) m_reparenting = !m_reparenting;
    if (InputManager::IsKeyPressed(GLFW_KEY_BACKSPACE)) {
        m_selected = NULL_ENTITY;
        m_reparenting = false;
    }
}

void Inspector::draw(Engine& engine) {
    World& world = engine.world();
    const glm::vec3 white(1.0f);
    const glm::vec3 accent(1.0f, 0.9f, 0.2f);
    float y = 700.0f;
    const float x = 10.0f;
    const auto line = [&](const std::string& text, const glm::vec3& color) {
        engine.AddText(text, x, y, 0.5f, color);
        y -= 20.0f;
    };

    line("Inspector", accent);
    line("click pick  H hide  C clone", white);
    line("G reparent  Del destroy  Ctrl+Z/Y undo", white);
    line(std::format("entities alive: {}", world.aliveCount()), white);
    if (!world.isAlive(m_selected)) {
        line("nothing selected", white);
        return;
    }

    const EntityHandle e = m_selected;
    const glm::vec3 pos = world.getWorldPosition(e);
    const glm::vec3 scale = world.getScale(e);
    const EntityHandle parent = world.getParent(e);
    const std::string name = world.getName(e).empty() ? "(unnamed)" : world.getName(e);
    line(std::format("{}  #{} gen {}", name, e.index, e.generation), accent);
    line(std::format("parent: {}", parent.isNull() ? std::string("none") : std::format("#{}", parent.index)), white);
    line(std::format("children: {}", world.getChildren(e).size()), white);
    line(std::format("world pos: {:.2f}, {:.2f}, {:.2f}", pos.x, pos.y, pos.z), white);
    line(std::format("scale: {:.2f}, {:.2f}, {:.2f}", scale.x, scale.y, scale.z), white);
    line(std::format("mesh {} visible {} layer {:#x}", world.getMesh(e), world.isVisible(e), world.getLayer(e)), white);
    if (!world.getTag(e).empty()) line(std::format("tag: {}", world.getTag(e)), white);
    if (m_reparenting) line("reparent: click new parent (empty space = root)", accent);

    const glm::vec4 bounds = world.getWorldBounds(e);
    if (bounds.w > 0.0f) engine.debugSphere(glm::vec3(bounds), bounds.w, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
    if (!parent.isNull()) engine.debugLine(world.getWorldPosition(parent), pos, glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
}
