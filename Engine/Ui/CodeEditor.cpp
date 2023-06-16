#include "../../include_all.h"
#include "CodeEditor.h"

// Declare variables to track button clicks
static int clickCount = 0;
static float clickTimer = 0.0f;



void CodeEditor()
{
    ImGui::Begin("Code Editor Window", NULL);

    ImVec2 size = ImGui::GetContentRegionAvail();
    bool save_file = ImGui::ImageButton((ImTextureID)&save_texture, ImVec2(64, 64));

    if (save_file)
    {
        cout << "Saving file..." << endl;

        std::ofstream file(code_editor_script_path);

        if (file.is_open()) {
            file << code;
            file.close();
        } else {
            std::cout << "Unable to open file." << std::endl;
        }
    }


    ImGui::SameLine();


    // Inside your ImGui window loop
    bool hot_reload = ImGui::ImageButton((ImTextureID)&hot_reload_texture, ImVec2(64, 64));

    if (hot_reload)
    {
        // Increment the click count and update the click timer
        clickCount++;
        clickTimer = ImGui::GetTime();

        // Check for single click after a delay of 0.3 seconds
        if (clickCount == 1)
        {
            std::cout << "Performing action for a single click" << std::endl;
            clickCount = 1;
        }
        else if (clickCount == 2)
        {
            std::cout << "Performing action for a double click" << std::endl;
            clickCount = 0;
        }
    }

    if (clickCount == 1 && ImGui::GetTime() - clickTimer > 0.4f)
        clickCount = 0;

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("1 click to reload this script\n"
                    "2 clicks to reload all scripts");
        ImGui::EndTooltip();
    }



    editor.Render("TextEditor");
    code = editor.GetText();


    ImGui::End();
}