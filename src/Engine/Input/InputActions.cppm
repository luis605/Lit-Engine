module;

#include <GLFW/glfw3.h>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.inputactions;

import Engine.input;
import Engine.Replay;

export class InputActions {
  public:
    void bindKey(const std::string& action, int key) { m_actions[action].keys.push_back(key); }
    void bindMouseButton(const std::string& action, int button) { m_actions[action].mouseButtons.push_back(button); }
    void bindAxis(const std::string& axis, int negativeKey, int positiveKey) { m_axes[axis].push_back({negativeKey, positiveKey}); }
    void unbind(const std::string& name) {
        m_actions.erase(name);
        m_axes.erase(name);
    }

    void setSource(const SimInput* source) { m_source = source; }

    [[nodiscard]] bool pressed(const std::string& action) const {
        return any(
            action, [this](int key) { return m_source ? m_source->pressed(key) : InputManager::IsKeyPressed(key); },
            [this](int button) { return m_source ? m_source->buttonPressed(button) : InputManager::IsMouseButtonPressed(button); });
    }
    [[nodiscard]] bool held(const std::string& action) const {
        return any(
            action, [this](int key) { return m_source ? m_source->held(key) : InputManager::IsKeyHeld(key); },
            [this](int button) { return m_source ? m_source->buttonHeld(button) : InputManager::IsMouseButtonHeld(button); });
    }
    [[nodiscard]] bool released(const std::string& action) const {
        return any(
            action, [this](int key) { return m_source ? m_source->released(key) : InputManager::IsKeyReleased(key); },
            [this](int button) { return m_source ? m_source->buttonReleased(button) : InputManager::IsMouseButtonReleased(button); });
    }

    [[nodiscard]] float axis(const std::string& name) const {
        const auto found = m_axes.find(name);
        if (found == m_axes.end()) return 0.0f;
        float value = 0.0f;
        for (const auto& [negative, positive] : found->second) {
            if (isHeld(negative)) value -= 1.0f;
            if (isHeld(positive)) value += 1.0f;
        }
        return std::clamp(value, -1.0f, 1.0f);
    }

  private:
    [[nodiscard]] bool isHeld(int key) const { return m_source ? m_source->held(key) : InputManager::IsKeyHeld(key); }

    struct Binding {
        std::vector<int> keys;
        std::vector<int> mouseButtons;
    };

    template <typename KeyFn, typename MouseFn>
    bool any(const std::string& action, KeyFn keyPredicate, MouseFn mousePredicate) const {
        const auto found = m_actions.find(action);
        if (found == m_actions.end()) return false;
        for (const int key : found->second.keys) {
            if (keyPredicate(key)) return true;
        }
        for (const int button : found->second.mouseButtons) {
            if (mousePredicate(button)) return true;
        }
        return false;
    }

    const SimInput* m_source = nullptr;
    std::unordered_map<std::string, Binding> m_actions;
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> m_axes;
};
