module;

#include <GLFW/glfw3.h>
#include <string>
#include <unordered_map>
#include <vector>

export module Engine.inputactions;

import Engine.input;

export class InputActions {
  public:
    void bindKey(const std::string& action, int key) { m_actions[action].keys.push_back(key); }
    void bindMouseButton(const std::string& action, int button) { m_actions[action].mouseButtons.push_back(button); }
    void bindAxis(const std::string& axis, int negativeKey, int positiveKey) { m_axes[axis].push_back({negativeKey, positiveKey}); }
    void unbind(const std::string& name) {
        m_actions.erase(name);
        m_axes.erase(name);
    }

    [[nodiscard]] bool pressed(const std::string& action) const {
        return any(action, InputManager::IsKeyPressed, InputManager::IsMouseButtonPressed);
    }
    [[nodiscard]] bool held(const std::string& action) const {
        return any(action, InputManager::IsKeyHeld, InputManager::IsMouseButtonHeld);
    }
    [[nodiscard]] bool released(const std::string& action) const {
        return any(action, InputManager::IsKeyReleased, InputManager::IsMouseButtonReleased);
    }

    [[nodiscard]] float axis(const std::string& name) const {
        const auto it = m_axes.find(name);
        if (it == m_axes.end()) return 0.0f;
        float value = 0.0f;
        for (const auto& [negative, positive] : it->second) {
            if (InputManager::IsKeyHeld(negative)) value -= 1.0f;
            if (InputManager::IsKeyHeld(positive)) value += 1.0f;
        }
        return value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
    }

  private:
    struct Binding {
        std::vector<int> keys;
        std::vector<int> mouseButtons;
    };

    template <typename KeyFn, typename MouseFn>
    bool any(const std::string& action, KeyFn keyFn, MouseFn mouseFn) const {
        const auto it = m_actions.find(action);
        if (it == m_actions.end()) return false;
        for (int key : it->second.keys) {
            if (keyFn(key)) return true;
        }
        for (int button : it->second.mouseButtons) {
            if (mouseFn(button)) return true;
        }
        return false;
    }

    std::unordered_map<std::string, Binding> m_actions;
    std::unordered_map<std::string, std::vector<std::pair<int, int>>> m_axes;
};
