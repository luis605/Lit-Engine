#include "../../include_all.h"

bool appearance_window_enabled = false;


void Appearance()
{

    ImGui::Begin("Appearance", &appearance_window_enabled);



    ImGui::Text("Themes: ");
    bool theme_blue = ImGui::Button("Blue");
    bool theme_high_contrast = ImGui::Button("High Contrast");
    bool theme_gray = ImGui::Button("Gray");
    bool theme_custom = ImGui::Button("Custom");

    if (theme_high_contrast)
    {
        SetStyleHighContrast(&ImGui::GetStyle());
    }
    else if (theme_gray)
    {
        SetStyleGray(&ImGui::GetStyle());
    }
    else if (theme_custom)
    {
        createNewThemeWindow_open = true;
    }

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
                cout << "Saving Project..." << endl;
                SaveProject();
            }

            if (ImGui::MenuItem("Save as", "Ctrl+Shift+S"))
            {
                cout << "Saving Project as..." << endl;
            }

            if (ImGui::MenuItem("Open", "Ctrl+O"))
            {
                cout << "Opening Project..." << endl;
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
    CreateNewTheme();
}