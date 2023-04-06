#include "../../include_all.h"

Texture2D save_texture;


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

    editor.Render("TextEditor");
    code = editor.GetText();


    ImGui::End();
}