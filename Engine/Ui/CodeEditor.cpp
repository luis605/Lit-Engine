#include <string>


void CodeEditor(string code)
{
    ImGui::Begin("Code Editor Window", NULL);
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImGui::InputTextMultiline("##Code", code.c_str(), code.size(), size);
    ImGui::End();
}