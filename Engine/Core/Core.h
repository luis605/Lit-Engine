#ifndef CORE_H
#define CORE_H

void Startup();
void EngineMainLoop();
void CleanUp();
Vector2 GetGlobalMousePosition();
void DraggableWindow();
void ToggleMaximization();
void ExitWindowRequested();
Vector3 glm3ToVec3(glm::vec3& vec3);
glm::vec3 vec3ToGlm3(Vector3& vec3);

int windowWidth = 100;
int windowHeight = 50;
int windowX = 0;
int windowY = 0;

bool isDragging = false;
ImVec2 windowPosition;

ImGuiIO *io;

static Image windowIconImage;
Texture2D windowIconTexture;

// Window States
bool isWindowMaximized = false;

bool exitWindowRequested = false;
bool exitWindow = false;


static std::unordered_map<std::string, ImFont*> s_Fonts;

RenderTexture downsamplerTexture;
RenderTexture upsamplerTexture;

ImVec2 windowOriginalPos;
ImVec2 lastMousePosition;


#endif // CORE_H