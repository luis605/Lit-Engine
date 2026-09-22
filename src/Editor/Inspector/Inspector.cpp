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

void Inspector::reset() {
    m_selected = NULL_ENTITY;
    m_reparenting = false;
    if (m_history) m_history->clear();
    m_hierarchy.reset();
}

namespace {
constexpr int kEditNone = 0;
constexpr int kEditName = 1;
constexpr int kEditTag = 2;
constexpr int kEditComponent = 3;
}

void Inspector::beginEdit(int target, const std::string& initial) {
    m_editTarget = target;
    m_editor.begin(initial);
}

void Inspector::handleEditing(Engine& engine) {
    World& world = engine.world();
    if (!world.isAlive(m_selected)) {
        m_editor.cancel();
        m_editTarget = kEditNone;
        return;
    }
    m_editor.insert(InputManager::TypedText());
    const bool ctrl = InputManager::IsKeyHeld(GLFW_KEY_LEFT_CONTROL) || InputManager::IsKeyHeld(GLFW_KEY_RIGHT_CONTROL);
    for (int key : InputManager::KeyEvents()) {
        EditResult result = EditResult::None;
        switch (key) {
            case GLFW_KEY_LEFT: result = m_editor.press(ctrl ? EditKey::WordLeft : EditKey::Left); break;
            case GLFW_KEY_RIGHT: result = m_editor.press(ctrl ? EditKey::WordRight : EditKey::Right); break;
            case GLFW_KEY_HOME: result = m_editor.press(EditKey::Home); break;
            case GLFW_KEY_END: result = m_editor.press(EditKey::End); break;
            case GLFW_KEY_BACKSPACE: result = m_editor.press(ctrl ? EditKey::Clear : EditKey::Backspace); break;
            case GLFW_KEY_DELETE: result = m_editor.press(EditKey::Delete); break;
            case GLFW_KEY_ENTER:
            case GLFW_KEY_KP_ENTER: result = m_editor.press(EditKey::Enter); break;
            case GLFW_KEY_ESCAPE: result = m_editor.press(EditKey::Escape); break;
            default: break;
        }
        if (result == EditResult::Committed) {
            if (m_editTarget == kEditName) {
                m_history->setName(m_selected, m_editor.text());
            } else if (m_editTarget == kEditTag) {
                m_history->setTag(m_selected, m_editor.text());
            } else if (m_editTarget == kEditComponent) {
                if (!m_history->setComponentText(m_selected, m_editComponent, m_editor.text())) Lit::Log::Warn("Could not apply '{}' to component {}", m_editor.text(), m_editComponent);
            }
            m_editTarget = kEditNone;
        } else if (result == EditResult::Cancelled) {
            m_editTarget = kEditNone;
        }
        if (!m_editor.active()) break;
    }
}

void Inspector::update(Engine& engine, bool pickingEnabled) {
    World& world = engine.world();
    if (!m_history) m_history = std::make_unique<History>(world);
    if (!world.isAlive(m_selected)) {
        m_selected = NULL_ENTITY;
        m_reparenting = false;
    }
    if (m_editor.active()) {
        handleEditing(engine);
        draw(engine);
        return;
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

    const EntityDescription description = world.describeEntity(m_selected);
    if (InputManager::IsKeyPressed(GLFW_KEY_F3)) beginEdit(kEditName, description.name);
    if (InputManager::IsKeyPressed(GLFW_KEY_F4)) beginEdit(kEditTag, description.tag);
    if (!description.components.empty()) {
        if (InputManager::IsKeyPressed(GLFW_KEY_PERIOD)) m_focusComponent = (m_focusComponent + 1) % description.components.size();
        if (InputManager::IsKeyPressed(GLFW_KEY_COMMA)) m_focusComponent = (m_focusComponent + description.components.size() - 1) % description.components.size();
        m_focusComponent = std::min(m_focusComponent, description.components.size() - 1);
        if (InputManager::IsKeyPressed(GLFW_KEY_F6)) {
            m_editComponent = description.components[m_focusComponent].first;
            beginEdit(kEditComponent, description.components[m_focusComponent].second);
        }
    }

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
    line("F3 name  F4 tag  ,/. component  F6 edit", white);
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
    const EntityDescription description = world.describeEntity(e);
    line(std::format("children: {}", description.childCount), white);
    line(std::format("world pos: {:.2f}, {:.2f}, {:.2f}", pos.x, pos.y, pos.z), white);
    line(std::format("scale: {:.2f}, {:.2f}, {:.2f}", scale.x, scale.y, scale.z), white);
    line(std::format("mesh {} visible {} layer {:#x}", world.getMesh(e), world.isVisible(e), world.getLayer(e)), white);
    if (!world.getTag(e).empty()) line(std::format("tag: {}", world.getTag(e)), white);
    for (size_t i = 0; i < description.components.size(); ++i) {
        const bool focused = i == m_focusComponent;
        line(std::format("{} {}: {}", focused ? ">" : " ", description.components[i].first, description.components[i].second), focused ? accent : white);
    }
    for (const auto& script : description.scripts) line(std::format("  script {}: {}", script.first, script.second), white);
    if (m_reparenting) line("reparent: click new parent (empty space = root)", accent);
    if (m_editor.active()) {
        const char* what = m_editTarget == kEditName ? "name" : (m_editTarget == kEditTag ? "tag" : m_editComponent.c_str());
        line(std::format("edit {}: {}", what, m_editor.display()), accent);
        line("Enter apply  Esc cancel  Ctrl+Backspace clear", white);
    }

    const glm::vec4 bounds = world.getWorldBounds(e);
    if (bounds.w > 0.0f) engine.debugSphere(glm::vec3(bounds), bounds.w, glm::vec4(1.0f, 0.9f, 0.2f, 1.0f));
    if (!parent.isNull()) engine.debugLine(world.getWorldPosition(parent), pos, glm::vec4(0.2f, 1.0f, 0.4f, 1.0f));
}
