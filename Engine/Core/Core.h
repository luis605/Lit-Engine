#ifndef CORE_H
#define CORE_H

void Startup();
void EngineMainLoop();
void CleanUp();
void DraggableWindow();
void ToggleMaximization();
void ExitWindowRequested();
Vector2 GetGlobalMousePosition();
Vector3 glm3ToVec3(glm::vec3& vec3);
glm::vec3 vec3ToGlm3(Vector3& vec3);

int windowWidth = 100;
int windowHeight = 50;
int windowX = 0;
int windowY = 0;

ImGuiIO *io;

static Image windowIconImage;
Texture2D windowIconTexture;

bool isWindowMaximized = false;
bool exitWindowRequested = false;
bool exitWindow = false;
bool isDraggingWindow = false;

static std::unordered_map<std::string, ImFont*> s_Fonts;

ImVec2 windowOriginalPos;
ImVec2 ImLastMousePosition;
ImVec2 windowPosition;

#endif // CORE_H