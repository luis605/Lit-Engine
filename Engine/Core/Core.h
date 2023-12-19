#ifndef CORE_H
#define CORE_H

void Startup();
void EngineMainLoop();
void CleanUp();
Vector2 GetGlobalMousePosition();
void DraggableWindow();
void ToggleMaximization();
void ExitWindowRequested();


int windowWidth = 100;
int windowHeight = 50;
int windowX = 0;
int windowY = 0;

bool isDragging = false;
ImVec2 windowPosition;


static Image window_icon_image;
Texture2D window_icon_texture;

// Window States
bool isWindowMaximized = false;

bool exitWindowRequested = false;
bool exitWindow = false;


static std::unordered_map<std::string, ImFont*> s_Fonts;

RenderTexture downsamplerTexture;
RenderTexture upsamplerTexture;

#endif // CORE_H