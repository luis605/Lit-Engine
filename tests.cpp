#include "include/raylib.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "include/rlImGui.h"

#include <string>
#include <iostream>


using namespace std;


int main() {
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(800, 600, "raylib [core] example - basic window");
    Texture2D texture = LoadTexture("logo1.png");

    std::cout << "Texture Format: " << texture.format << std::endl;
    std::cout << "Texture Width: " << texture.width << std::endl;
    std::cout << "Texture Height: " << texture.height << std::endl;
    std::cout << "Texture ID: " << texture.id << std::endl;
    std::cout << "Texture Mipmaps: " << texture.mipmaps << std::endl;
    
    
    CloseWindow();
}