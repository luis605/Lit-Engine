module;

#include <GLFW/glfw3.h>
#include <array>

import Engine.glm;

module Engine.input;

std::array<InputManager::KeyState, GLFW_KEY_LAST + 1> InputManager::m_keyStates;
std::array<InputManager::KeyState, GLFW_MOUSE_BUTTON_LAST + 1> InputManager::m_mouseButtonStates;
glm::vec2 InputManager::m_currentMousePos(0.0f, 0.0f);
glm::vec2 InputManager::m_previousMousePos(0.0f, 0.0f);
bool InputManager::m_firstMouseMove = true;

void InputManager::Init(GLFWwindow* window) {
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, [](GLFWwindow* w, double x, double y) {
        CursorPosCallback(w, glm::vec2(static_cast<float>(x), static_cast<float>(y)));
    });

    m_keyStates.fill(KeyState::None);
    m_mouseButtonStates.fill(KeyState::None);
    m_firstMouseMove = true;
}

void InputManager::Update() {
    for (auto& state : m_keyStates) {
        if (state == KeyState::Pressed) {
            state = KeyState::Held;
        } else if (state == KeyState::Released) {
            state = KeyState::None;
        }
    }

    for (auto& state : m_mouseButtonStates) {
        if (state == KeyState::Pressed) {
            state = KeyState::Held;
        } else if (state == KeyState::Released) {
            state = KeyState::None;
        }
    }

    m_previousMousePos = m_currentMousePos;
}

bool InputManager::IsKeyPressed(int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keyStates[key] == KeyState::Pressed;
}
bool InputManager::IsKeyReleased(int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keyStates[key] == KeyState::Released;
}
bool InputManager::IsKeyHeld(int key) {
    if (key < 0 || key > GLFW_KEY_LAST) return false;
    return m_keyStates[key] == KeyState::Held;
}

bool InputManager::IsMouseButtonPressed(int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_mouseButtonStates[button] == KeyState::Pressed;
}
bool InputManager::IsMouseButtonReleased(int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_mouseButtonStates[button] == KeyState::Released;
}
bool InputManager::IsMouseButtonHeld(int button) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return false;
    return m_mouseButtonStates[button] == KeyState::Held;
}

glm::vec2 InputManager::GetMousePosition() { return m_currentMousePos; }
glm::vec2 InputManager::GetMouseDelta() { return m_currentMousePos - m_previousMousePos; }

void InputManager::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) {
        m_keyStates[key] = KeyState::Pressed;
    } else if (action == GLFW_RELEASE) {
        m_keyStates[key] = KeyState::Released;
    }
}

void InputManager::MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    if (action == GLFW_PRESS) {
        m_mouseButtonStates[button] = KeyState::Pressed;
    } else if (action == GLFW_RELEASE) {
        m_mouseButtonStates[button] = KeyState::Released;
    }
}

void InputManager::CursorPosCallback(GLFWwindow* window, glm::vec2 pos) {
    if (m_firstMouseMove) {
        m_previousMousePos = pos;
        m_currentMousePos = pos;
        m_firstMouseMove = false;
    } else {
        m_currentMousePos = pos;
    }
}
