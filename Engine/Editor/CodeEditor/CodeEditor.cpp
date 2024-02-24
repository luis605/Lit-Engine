#include "../../include_all.h"
#include "CodeEditor.h"

// Declare variables to track button clicks
static int clickCount = 0;
static float clickTimer = 0.0f;



void CodeEditor()
{
    ImGui::Begin(ICON_FA_CODE " Code Editor", NULL);

    ImVec2 size = ImGui::GetContentRegionAvail();
    bool saveFile = ImGui::ImageButton((ImTextureID)&saveTexture, ImVec2(64, 64));

    if (saveFile)
    {
        cout << "Saving file..." << endl;

        std::ofstream file(codeEditorScriptPath);

        if (file.is_open()) {
            file << code;
            file.close();
        } else {
            std::cout << "Unable to open file." << std::endl;
        }
    }

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Save file");
        ImGui::EndTooltip();
    }


    ImGui::SameLine();


    // Inside your ImGui window loop
    bool hot_reload = ImGui::ImageButton((ImTextureID)&hotReloadTexture, ImVec2(64, 64));

    if (hot_reload)
    {
        clickCount++;
        clickTimer = ImGui::GetTime();

        if (clickCount == 1)
        {
            clickCount = 1;
        }
        else if (clickCount == 2)
        {
            firstTimeGameplay = true;
            for (Entity& entity : entitiesList)
                entity.running = false;
                
            clickCount = 0;
        }
    }

    if (clickCount == 1 && ImGui::GetTime() - clickTimer > 0.4f)
        clickCount = 0;

    if (ImGui::IsItemHovered())
    {
        ImGui::BeginTooltip();
        ImGui::Text("Reload all scripts");
        ImGui::EndTooltip();
    }



    editor.Render("TextEditor");
    code = editor.GetText();


    ImGui::End();
}