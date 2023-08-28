#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"
#include "include/raylib.h"
#include <iostream>
int main(int argc, char* argv[])
{
	int screenWidth = 1280;
	int screenHeight = 800;

	SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
	InitWindow(screenWidth, screenHeight, "raylib-Extras [ImGui] example - simple ImGui Demo");
	SetTargetFPS(144);
	rlImGuiSetup(true);
	
	while (!WindowShouldClose())    
	{
		BeginDrawing();
		ClearBackground(DARKGRAY);
		
		rlImGuiBegin();

        bool readOnly = true;
		float size_x = 10.0f;

        ImGui::SliderFloat("##ScaleX", &size_x, 0, 100, "%.3f", readOnly ? ImGuiSliderFlags_ReadOnly : 0);

        if (ImGui::Button("Hi There", ImVec2(100,100)))
        {
            std::cout << "pressed\n";
        }

        ImGui::Begin("Another Window");

        if (ImGui::Button("Hi There 2", ImVec2(100,100)))
        {
            std::cout << "pressed\n";
        }

        ImGui::End();
        
		
		rlImGuiEnd();

		EndDrawing();
		
	}
	
    rlImGuiShutdown();
	CloseWindow();

	return 0;
}

