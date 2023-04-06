#include "raylib.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"
#include "ImGuiColorTextEdit/TextEditor.h"
#include <iostream>
#include <string>

int main(void)
{
    // Initialization
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "TextEditor with raylib");
    rlImGuiSetup(true);
    // Initialize the TextEditor state
	TextEditor editor;
	auto lang = TextEditor::LanguageDefinition::CPlusPlus();

    // Main game loop
    while (!WindowShouldClose())
    {
        // Start ImGui frame
        rlImGuiBegin();

        BeginDrawing();
        ClearBackground(GRAY);

        // Draw the text editor
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));
        ImGui::Begin("Text Editor");
        editor.Render("TextEditor");

        std::string text = editor.GetText();
        std::cout << text.c_str() << std::endl;
        
        ImGui::End();

        // End ImGui frame
        rlImGuiEnd();

        // Draw
        EndDrawing();
    }

    // De-Initialization
    rlImGuiShutdown();
    CloseWindow();

    return 0;
}