#include "Core.hpp"
#include "global_variables.hpp"

#include <Engine/Core/SaveLoad.hpp>
#include <extras/IconsFontAwesome6.h>
#include <rlImGui.h>
#include <rlgl.h>

#include <Engine/Core/Entity.hpp>
#include <Engine/Core/Textures.hpp>
#include <Engine/Editor/AssetsExplorer/AssetsExplorer.hpp>
#include <Engine/Editor/CodeEditor/CodeEditor.hpp>
#include <Engine/Editor/ObjectsList/ObjectsList.hpp>
#include <Engine/Editor/Inspector/Inspector.hpp>
#include <Engine/Editor/MaterialsNodeEditor/MaterialNodeEditor.hpp>
#include <Engine/Editor/MenuBar/MenuBar.hpp>
#include <Engine/Editor/MenuBar/Settings.hpp>
#include <Engine/Editor/SceneEditor/Gizmo/Gizmo.hpp>
#include <Engine/Editor/SceneEditor/SceneEditor.hpp>
#include <Engine/Editor/Styles/ImGuiExtras.hpp>
#include <Engine/Editor/Styles/Styles.hpp>
#include <Engine/Lighting/InitLighting.hpp>
#include <Engine/Lighting/lights.hpp>
#include <Engine/Lighting/skybox.hpp>
#include <Engine/Plugins/Loader.hpp>
#include <Engine/Scripting/functions.hpp>
#include <Engine/Scripting/math.hpp>

#define STRESS_TEST false
#if STRESS_TEST
#include "stressTest.cpp"
#endif

int windowWidth = 100;
int windowHeight = 50;
Texture2D windowIconTexture = { 0 };
bool isWindowMaximized = false;
bool exitWindowRequested = false;
bool exitWindow = false;
bool isDraggingWindow = false;
bool fontsNeedUpdate = false;
ImVec2 windowOriginalPos = ImVec2(0, 0);
ImVec2 ImLastMousePosition = ImVec2(0, 0);
ImVec2 windowPosition = ImVec2(0, 0);

ImGuiViewport* viewport = nullptr;

void InitLitWindow() {
    SetTraceLogLevel(LOG_WARNING);

    InitWindow(windowWidth, windowHeight, "Lit Engine - Initializing");
    windowWidth = GetMonitorWidth(0) * 0.95;
    windowHeight = GetMonitorHeight(0) * 0.9;
    CloseWindow();

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE |
                   FLAG_WINDOW_UNDECORATED);
    InitWindow(windowWidth, windowHeight, "Lit Engine");

    SetExitKey(KEY_NULL);
}

void LoadTextures() {
    folderTexture = LoadTexture("Assets/images/folder.png");
    imageTexture = LoadTexture("Assets/images/image_file_type.png");
    cppTexture = LoadTexture("Assets/images/cpp_file_type.png");
    pythonTexture = LoadTexture("Assets/images/python_file_type.png");
    modelTexture = LoadTexture("Assets/images/model_file_type.png");
    materialTexture = LoadTexture("Assets/images/material_file_type.png");
    emptyTexture = LoadTexture("Assets/images/empty_file_file_type.png");
    runTexture = LoadTexture("Assets/images/run_game.png");
    pauseTexture = LoadTexture("Assets/images/pause_game.png");
    saveTexture = LoadTexture("Assets/images/save_file.png");
    hotReloadTexture = LoadTexture("Assets/images/hot_reload.png");
    lightTexture = LoadTexture("Assets/images/light_bulb.png");
    noiseTexture = LoadTexture("Assets/images/noise.png");
    windowIconImage = LoadImage("Assets/images/window_icon.png");
    windowIconTexture = LoadTextureFromImage(windowIconImage);
    verticalBlurTexture =
        LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    horizontalBlurTexture =
        LoadRenderTexture(GetScreenWidth(), GetScreenHeight());
    upsamplerTexture = LoadRenderTexture(GetScreenWidth(), GetScreenHeight());

    SetWindowIcon(windowIconImage);
}

void UpdateFonts(const float& fontSize, ImGuiIO& io) {
    s_Fonts.clear();

    fs::path fontPath = GetWorkingDirectory();
    fontPath += "/Assets/fonts/";

    auto addFont = [&](const std::string& fontName, const std::string& fileName,
                       float sizeModifier = 0) {
        s_Fonts[fontName] = io.Fonts->AddFontFromFileTTF(
            (fontPath.string() + fileName).c_str(), fontSize + sizeModifier);
    };

    addFont("Regular", "JetBrainsMono-Regular.ttf");

    // Enable MergeMode and add Font Awesome icons
    ImFontConfig config;
    config.MergeMode = true;
    static const ImWchar icon_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    io.Fonts->AddFontFromFileTTF(
        (fontPath.string() + "fontawesome-webfont.ttf").c_str(), fontSize,
        &config, icon_ranges);

    io.FontDefault = s_Fonts["Regular"];

    rlImGuiReloadFonts();
}

void HandleFontUpdateIfNeeded() {
    if (fontsNeedUpdate) {
        ImGuiIO& io = ImGui::GetIO();
        UpdateFonts(editorFontSize, io);
        fontsNeedUpdate = false;
    }
}

void InitImGui() {
    rlImGuiSetup(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    viewport = ImGui::GetMainViewport();

    SetStyleGray(&ImGui::GetStyle());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowMinSize.x = 370.0f;

    float fontSize = 17.0f * io.FontGlobalScale;

    UpdateFonts(fontSize, io);
}

void InitShaders() {
    shader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                        "Engine/Lighting/shaders/lighting_fragment.glsl");
    instancingShader =
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                   "Engine/Lighting/shaders/lighting_fragment.glsl");
    horizontalBlurShader =
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                   "Engine/Lighting/shaders/blurHorizontal.fs");
    verticalBlurShader =
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                   "Engine/Lighting/shaders/blurVertical.fs");
    upsamplerShader = LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                                 "Engine/Lighting/shaders/upsampler.glsl");
    downsampleShader =
        LoadShader("Engine/Lighting/shaders/lighting_vertex.glsl",
                   "Engine/Lighting/shaders/downsampler.glsl");

    char* shaderCode =
        LoadFileText("Engine/Lighting/shaders/luminanceCompute.glsl");
    unsigned int shaderData = rlCompileShader(shaderCode, RL_COMPUTE_SHADER);
    exposureShaderProgram = rlLoadComputeShaderProgram(shaderData);
    UnloadFileText(shaderCode);
}

void InitCodeEditor() {
    code.resize(10);
    auto lang = TextEditor::LanguageDefinition::Python();
    editor.SetLanguageDefinition(lang);
}

void InitSceneEditor() {
    lightModel = LoadModelFromMesh(GenMeshPlane(4, 4, 1, 1));
}

void LoadFilesystem() {
    fs::path path = GetWorkingDirectory();
    path += "/project/";

    if (!fs::exists(path)) {
        fs::create_directory(path);
    }

    fs::path gameProjectPath = path / "game";
    if (!fs::exists(gameProjectPath)) {
        fs::create_directory(gameProjectPath);
    }

    fs::path themesPath = path / "themes";
    if (!fs::exists(themesPath)) {
        fs::create_directory(themesPath);
    }
}

void InitEngine() {
    entityModule = createEntityModule();
}

void Startup() {
    InitLitWindow();

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_STENCIL_TEST);

    skybox.loadSkybox("Assets/images/skybox/default skybox.hdr");
    Py_Initialize();
    InitImGui();
    LoadTextures();
    InitShaders();
    InitLighting();
    InitGizmo();
    InitEditorCamera();
    InitCodeEditor();
    InitSceneEditor();
    InitRenderModelPreviewer();
    loadAllPlugins();
    LoadFilesystem();
    InitEngine();

#if STRESS_TEST
    InitStressTest();
#endif
}

void EngineMainLoop() {
    while ((!exitWindow)) {
        if (WindowShouldClose())
            exitWindowRequested = true;

        HandleFontUpdateIfNeeded();
        AsyncTextureManager::ProcessPendingUpdates();

        BeginDrawing();

        ClearBackground(DARKGRAY);

        rlImGuiBegin();
        ImGui::DockSpaceOverViewport(ImGui::GetID("MyDockSpace"), viewport);

        MenuBar();
        AssetsExplorer();
        CodeEditor();
        EntitiesList();
        Inspector();
        EditorCamera();
        pluginManager.updateAll();

        rlImGuiEnd();

        EndDrawing();
    }
}

void CleanUp() {
    TraceLog(LOG_INFO, "Cleaning up...");

    inGamePreview = false;
    firstTimeGameplay = false;

    entitiesListPregame.clear();
    entitiesList.clear();
    lights.clear();

    UnloadShader(shader);
    UnloadImage(windowIconImage);
    UnloadTexture(folderTexture);
    UnloadTexture(imageTexture);
    UnloadTexture(cppTexture);
    UnloadTexture(pythonTexture);
    UnloadTexture(modelTexture);
    UnloadTexture(emptyTexture);
    UnloadTexture(runTexture);
    UnloadTexture(pauseTexture);
    UnloadTexture(saveTexture);
    UnloadTexture(hotReloadTexture);
    UnloadTexture(lightTexture);
    UnloadTexture(windowIconTexture);

    for (auto it = modelsIcons.begin(); it != modelsIcons.end(); ++it)
        UnloadTexture(it->second);

    UnloadModel(lightModel);
    modelsIcons.clear();
    lights.clear();

    pluginManager.unloadAll();
    rlImGuiShutdown();
    CloseWindow();
}

void DraggableWindow() {
    bool isTitleBarHovered = ImGui::IsItemHovered();
    ImVec2 currentMousePos = ImGui::GetMousePos();
    ImVec2 mouseDelta = ImVec2(currentMousePos.x - ImLastMousePosition.x,
                               currentMousePos.y - ImLastMousePosition.y);

    float moveThreshold = 3.f;

    if (isTitleBarHovered && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (!isDraggingWindow && (fabs(mouseDelta.x) > moveThreshold ||
                                  fabs(mouseDelta.y) > moveThreshold)) {
            isDraggingWindow = true;
            ImVec2 windowPos = ImGui::GetWindowPos();
            windowOriginalPos = ImVec2(currentMousePos.x - windowPos.x,
                                       currentMousePos.y - windowPos.y);
        }
    } else if (isDraggingWindow &&
               ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        isDraggingWindow = false;
    }

    if (isDraggingWindow) {
        if (isWindowMaximized)
            ToggleMaximization();

        ImVec2 newPosition =
            ImVec2(ImGui::GetMousePos().x - windowOriginalPos.x,
                   ImGui::GetMousePos().y - windowOriginalPos.y);
        float smoothFactor = 0.3f;
        newPosition.x =
            LitLerp(ImGui::GetWindowPos().x, newPosition.x * 2.f, smoothFactor);
        newPosition.y =
            LitLerp(ImGui::GetWindowPos().y, newPosition.y * 2.f, smoothFactor);
        SetWindowPosition(newPosition.x, newPosition.y);
    }

    ImLastMousePosition = currentMousePos;
}

void ToggleMaximization() {
    isWindowMaximized ? RestoreWindow() : MaximizeWindow();
    isWindowMaximized = !isWindowMaximized;
}

static int currentExitMenuButton = 1;

void ExitWindowRequested() {
    showManipulateEntityPopup = false;

    if (exitWindowRequested)
        ImGui::OpenPopup("Exit App");

    ImVec2 center(ImGui::GetIO().DisplaySize.x * 0.5f,
                  ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    const ImVec2 p = ImVec2(24.0f, 24.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, p);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);

    if (ImGui::BeginPopupModal("Exit App", NULL,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoTitleBar)) {
        ImGuiContext& g = *GImGui;
        const ImGuiStyle& style = g.Style;
        float margin = (style.WindowPadding.x) * 2.0f;
        float w = ImGui::GetWindowWidth() - margin;
        float h = 35.0f;

        ImGui::CenteredText("Do you want to save the project", ImVec2(w, 20));
        ImGui::CenteredText("before exiting?", ImVec2(w, 20));
        ImGui::CenteredText("\nYou can revert to undo the changes\n \n",
                            ImVec2(w, 40));

        if (ImGui::Button("Save", ImVec2(w, h))) {
            SaveProject();
            exitWindow = true;
        }

        const ImVec2 spc = ImVec2(7.0f, 16.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spc);

        if (ImGui::Button("Revert Changes", ImVec2(w, h))) {
            exitWindow = true;
        }

        if (ImGui::Button("Cancel", ImVec2(w, h))) {
            exitWindowRequested = false;
        }

        ImGui::PopStyleVar(1);
        ImGui::EndPopup();
    }

    ImGui::PopStyleVar(2);
}

Vector3 glm3ToVec3(const glm::vec3& vec3) {
    return (Vector3){vec3.x, vec3.y, vec3.z};
}

glm::vec3 vec3ToGlm3(const Vector3& vec3) {
    return (glm::vec3){vec3.x, vec3.y, vec3.z};
}