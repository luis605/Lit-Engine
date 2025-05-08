/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef CORE_H
#define CORE_H

#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui_internal.h>
#include <imgui.h>

#include <raylib.h>
#include <glm/glm.hpp>
#include <unordered_map>
#include <string>

void Startup();
void EngineMainLoop();
void CleanUp();
void DraggableWindow();
void ToggleMaximization();
void ExitWindowRequested();
Vector2 GetGlobalMousePosition();
Vector3 glm3ToVec3(const glm::vec3& vec3);
glm::vec3 vec3ToGlm3(const Vector3& vec3);

static std::unordered_map<std::string, ImFont*> s_Fonts;
static Image windowIconImage;

extern int windowWidth;
extern int windowHeight;
extern Texture2D windowIconTexture;
extern bool isWindowMaximized;
extern bool exitWindowRequested;
extern bool exitWindow;
extern bool isDraggingWindow;
extern bool fontsNeedUpdate;
extern ImVec2 windowOriginalPos;
extern ImVec2 ImLastMousePosition;
extern ImVec2 windowPosition;
extern ImGuiViewport* viewport;

#endif // CORE_H