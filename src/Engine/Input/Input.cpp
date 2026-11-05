module;

#include <GLFW/glfw3.h>
#include <array>
#include <string>
#include <vector>

import Engine.glm;

module Engine.input;

std::array<InputManager::KeyState, GLFW_KEY_LAST + 1> InputManager::m_keyStates;
std::array<InputManager::KeyState, GLFW_MOUSE_BUTTON_LAST + 1> InputManager::m_mouseButtonStates;
std::string InputManager::m_typed;
std::vector<int> InputManager::m_keyEvents;
glm::vec2 InputManager::m_currentMousePos(0.0f, 0.0f);
glm::vec2 InputManager::m_previousMousePos(0.0f, 0.0f);
bool InputManager::m_firstMouseMove = true;

namespace {
constexpr unsigned kAsciiLimit = 0x80;
constexpr unsigned kTwoByteLimit = 0x800;
constexpr unsigned kThreeByteLimit = 0x10000;
constexpr unsigned kContinuationMarker = 0x80;
constexpr unsigned kContinuationMask = 0x3F;
constexpr unsigned kTwoByteLead = 0xC0;
constexpr unsigned kThreeByteLead = 0xE0;
constexpr unsigned kFourByteLead = 0xF0;

void appendContinuation(std::string& out, unsigned codepoint, int shift) { out.push_back(static_cast<char>(kContinuationMarker | ((codepoint >> shift) & kContinuationMask))); }

void appendUtf8(std::string& out, unsigned codepoint) {
    if (codepoint < kAsciiLimit) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint < kTwoByteLimit) {
        out.push_back(static_cast<char>(kTwoByteLead | (codepoint >> 6)));
        appendContinuation(out, codepoint, 0);
    } else if (codepoint < kThreeByteLimit) {
        out.push_back(static_cast<char>(kThreeByteLead | (codepoint >> 12)));
        appendContinuation(out, codepoint, 6);
        appendContinuation(out, codepoint, 0);
    } else {
        out.push_back(static_cast<char>(kFourByteLead | (codepoint >> 18)));
        appendContinuation(out, codepoint, 12);
        appendContinuation(out, codepoint, 6);
        appendContinuation(out, codepoint, 0);
    }
}
}

void InputManager::Init(GLFWwindow* window) {
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, [](GLFWwindow* callbackWindow, double positionX, double positionY) {
        CursorPosCallback(callbackWindow, glm::vec2(static_cast<float>(positionX), static_cast<float>(positionY)));
    });

    m_keyStates.fill(KeyState::None);
    m_mouseButtonStates.fill(KeyState::None);
    m_firstMouseMove = true;
}

void InputManager::Update() {
    const auto advance = [](KeyState& state) {
        if (state == KeyState::Pressed) state = KeyState::Held;
        else if (state == KeyState::Released) state = KeyState::None;
    };
    for (KeyState& state : m_keyStates) advance(state);
    for (KeyState& state : m_mouseButtonStates) advance(state);

    m_previousMousePos = m_currentMousePos;
    m_typed.clear();
    m_keyEvents.clear();
}

bool InputManager::IsKeyPressed(int key) { return key >= 0 && key <= GLFW_KEY_LAST && m_keyStates[key] == KeyState::Pressed; }
bool InputManager::IsKeyReleased(int key) { return key >= 0 && key <= GLFW_KEY_LAST && m_keyStates[key] == KeyState::Released; }
bool InputManager::IsKeyHeld(int key) { return key >= 0 && key <= GLFW_KEY_LAST && m_keyStates[key] == KeyState::Held; }

bool InputManager::IsMouseButtonPressed(int button) { return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && m_mouseButtonStates[button] == KeyState::Pressed; }
bool InputManager::IsMouseButtonReleased(int button) { return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && m_mouseButtonStates[button] == KeyState::Released; }
bool InputManager::IsMouseButtonHeld(int button) { return button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST && m_mouseButtonStates[button] == KeyState::Held; }

const std::string& InputManager::TypedText() { return m_typed; }
const std::vector<int>& InputManager::KeyEvents() { return m_keyEvents; }

void InputManager::CharCallback(GLFWwindow*, unsigned int codepoint) { appendUtf8(m_typed, codepoint); }

glm::vec2 InputManager::GetMousePosition() { return m_currentMousePos; }
glm::vec2 InputManager::GetMouseDelta() { return m_currentMousePos - m_previousMousePos; }

void InputManager::KeyCallback(GLFWwindow*, int key, int, int action, int) {
    if (key < 0 || key > GLFW_KEY_LAST) return;
    if (action == GLFW_PRESS) {
        m_keyStates[key] = KeyState::Pressed;
        m_keyEvents.push_back(key);
    } else if (action == GLFW_REPEAT) {
        m_keyEvents.push_back(key);
    } else if (action == GLFW_RELEASE) {
        m_keyStates[key] = KeyState::Released;
    }
}

void InputManager::MouseButtonCallback(GLFWwindow*, int button, int action, int) {
    if (button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
    if (action == GLFW_PRESS) m_mouseButtonStates[button] = KeyState::Pressed;
    else if (action == GLFW_RELEASE) m_mouseButtonStates[button] = KeyState::Released;
}

void InputManager::CursorPosCallback(GLFWwindow*, glm::vec2 position) {
    if (m_firstMouseMove) {
        m_previousMousePos = position;
        m_firstMouseMove = false;
    }
    m_currentMousePos = position;
}
