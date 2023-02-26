#include "../../include_all.h"





bool nameBufferInitialized = false;
// Create character arrays for the text boxes
char modelPathBuffer[512] = {0};


string inspectorNameBuffer = "";


// Initialize variables to store the scale, position, and rotation values
Vector3 entity_scale = {1, 1, 1};
Vector3 entity_position = {0, 0, 0};
Vector3 entity_rotation = {0, 0, 0};
string entity_name = "";
Color entityColor = RED;
ImVec4 entityColorImGui = { 0, 0, 0, 0 };
bool changeEntityColorPopup = false;
bool setEntityColor = false;

std::string button_name;
int button_name_index;


void Inspector()
{
    // Get Entity
    string entity_name;

    if (!entities_list_pregame.empty()) entity_name = entity_in_inspector->name;
    else string entity_name = "None";


    // Update Vars:
    entity_position = entity_in_inspector->position;

    std::stringstream title;
    title << "Inspecting '" << entity_name << "'";
    ImGui::Text(title.str().c_str());


    // Draw the main content area with a rectangle
    ImGui::BeginChild("MainContent", ImVec2(0, 500));

    // Set the color of the button background
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    if (ImGui::Button("DELETE"))
    {
        entity_in_inspector->remove();
        // delete *entity_in_inspector;
        // entityList.erase(it);
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();


    ImGui::Text("Model Path:");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputText("##ModelPathInputBox", modelPathBuffer, 512);
    ImGui::PopStyleVar();

    ImGui::Text("Scale:");
    ImGui::InputFloat("X##ScaleX", &entity_scale.x);
    ImGui::InputFloat("Y##ScaleY", &entity_scale.y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputFloat("Z##ScaleZ", &entity_scale.z);
    ImGui::PopStyleVar();
    entity_in_inspector->scale = entity_scale;

    ImGui::Text("Position:");
    ImGui::InputFloat("X##PositionX", &entity_position.x);
    ImGui::InputFloat("Y##PositionY", &entity_position.y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputFloat("Z##PositionZ", &entity_position.z);
    ImGui::PopStyleVar();
    entity_in_inspector->position = entity_position;

    ImGui::Text("Color: ");
    ImGui::ColorEdit4("##ChangeEntityColor", (float*)&entityColorImGui, ImGuiColorEditFlags_NoInputs);
    Color entityColor = (Color){ (unsigned char)(entityColorImGui.x*255), (unsigned char)(entityColorImGui.y*255), (unsigned char)(entityColorImGui.z*255), (unsigned char)(color.w*255) };
    entity_in_inspector->color = entityColor;

    ImGui::Text("Physics: ");
    ImGui::Text("Do Physics ");
    ImGui::SameLine();
    ImGui::Checkbox("##", &do_physics);


    ImGui::Text("Scripts: ");
    std::string name = std::string("Drop Script Here: ") + button_name;
    ImGui::Text(name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("##Drag'n Drop", ImVec2(300,150)))
    {
    }

    
    if (ImGui::BeginDragDropTarget())
    {
        // Check if a drag and drop operation has been accepted
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_CELL"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            // Copy the button name to the variable outside the window
            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            button_name = path;
            button_name_index = 0;
            entity_in_inspector->script = button_name;
        }
        ImGui::EndDragDropTarget();
    }


    //std::cout << button_name << std::endl;
    


    ImGui::EndChild();
}


