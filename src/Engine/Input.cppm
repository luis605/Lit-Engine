module;

#include <GLFW/glfw3.h>

import glm;
import std;

export module input;

export class InputManager {
  public:
    InputManager() = delete;

    static void Init(GLFWwindow* window);
    static void Update();

    static bool IsKeyPressed(int key);
    static bool IsKeyReleased(int key);
    static bool IsKeyHeld(int key);

    static bool IsMouseButtonPressed(int button);
    static bool IsMouseButtonReleased(int button);
    static bool IsMouseButtonHeld(int button);

    static glm::vec2 GetMousePosition();
    static glm::vec2 GetMouseDelta();

  private:
    enum class KeyState { None, Pressed, Held, Released };

    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void CursorPosCallback(GLFWwindow* window, glm::vec2 pos);

    static std::array<KeyState, GLFW_KEY_LAST + 1> m_keyStates;
    static std::array<KeyState, GLFW_MOUSE_BUTTON_LAST + 1> m_mouseButtonStates;

    static glm::vec2 m_currentMousePos;
    static glm::vec2 m_previousMousePos;
};
