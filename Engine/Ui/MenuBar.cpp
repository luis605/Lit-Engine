#include "../../include_all.h"

bool appearance_window_enabled = true;


void Appearance()
{
    ImGui::Begin("Appearance", nullptr, ImGuiWindowFlags_NoCollapse);


    ImGui::Text("Themes: ");
    bool theme_blue = ImGui::Button("Blue");
    bool theme_gray = ImGui::Button("Gray");
    bool theme_custom = ImGui::Button("Custom");

    ImGui::End();
}

void MenuBar()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                std::cout << "Saving Project..." << std::endl;
                SaveProject();
            }

            if (ImGui::MenuItem("Save as", "Ctrl+Shift+S"))
            {
                std::cout << "Saving Project as..." << std::endl;
            }

            if (ImGui::MenuItem("Open", "Ctrl+O"))
            {
                std::cout << "Opening Project..." << std::endl;
                LoadProject();
            }

            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit"))
        {
            // Add menu items for "Edit" menu here
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Preferences"))
        {
            if (ImGui::MenuItem("Appearance", "Ctrl+Shift+D"))
            {
                appearance_window_enabled = true;
            }
            
            ImGui::EndMenu();
        }
        
        ImGui::EndMainMenuBar();
    }

    // Load preferences windows
    if (appearance_window_enabled) Appearance();
}