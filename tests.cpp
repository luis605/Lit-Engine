#include "include/raylib.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

#include <string>
#include <iostream>

using namespace std;


int main() {
    InitWindow(500, 500, "Example");

    // ImGui
    rlImGuiSetup(true);

    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig* fontConfig = new ImFontConfig();
    fontConfig->FontDataOwnedByAtlas = false;

    std::string fontPath = GetWorkingDirectory();
    fontPath += "/assets/fonts/Poppins-Regular.ttf";
    std::cout << fontPath << std::endl;

    ImFont* robotoFont = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 20.0f, fontConfig);
    if (!robotoFont) {
        std::cout << "Font loading failed!" << std::endl;
    }
    else {
        std::cout << "Font loaded successfully." << std::endl;
    }

    io.FontDefault = robotoFont;
    io.Fonts->Build();



    while (!WindowShouldClose()) {
        BeginDrawing();

            ClearBackground(DARKGRAY);

            rlImGuiBegin();

            ImGui::Begin("Test");
            ImGui::Text("Hello, world!");
            ImGui::End();

            rlImGuiEnd();

        EndDrawing();
    }
}