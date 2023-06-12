#include "../../include_all.h"


void EntityInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();

    selected_entity = get<Entity*>(object_in_inspector);

    entity_position = selected_entity->position;
    if (selected_entity->isChild)
        entity_position = selected_entity->relative_position;

    entity_scale = selected_entity->scale;
    entity_color = selected_entity->color;
    ImVec4 entity_colorImGui = ImVec4(
        entity_color.r / 255.0f,
        entity_color.g / 255.0f,
        entity_color.b / 255.0f,
        entity_color.a / 255.0f
    );

    string entity_name;

    if (!entities_list_pregame.empty()) {
        entity_name = selected_entity->name;
    }

    ImGui::Text("Inspecting '");
    ImGui::SameLine();

    float textWidth = ImGui::CalcTextSize(entity_name.c_str()).x + 10.0f;
    textWidth = std::max(textWidth, 100.0f);
    ImGui::SetNextItemWidth(textWidth);

    char inputBuffer[selected_entity->name.size() + 255];
    strcpy(inputBuffer, entity_name.c_str());

    if (ImGui::InputText("##Title Part 2", inputBuffer, sizeof(inputBuffer)))
        selected_entity->name = inputBuffer;



    ImGui::SameLine();
    ImGui::Text("'");


    ImGui::BeginChild("MainContent", window_size);


    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    if (ImGui::Button("DELETE"))
    {
        selected_entity->remove();
    }
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();


    ImGui::Text("Model Path:");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    string model_path_title = selected_entity->model_path + "##Drag'nDropModelPath";
    ImGui::Text("Drop Model Here: ");
    ImGui::SameLine();
    if (ImGui::Button(model_path_title.c_str(), ImVec2(200,25)));
    


    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_DEMO_CELL"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            selected_entity->model_path = path;
            selected_entity_model_path_index = 0;
            selected_entity->model_path = selected_entity->model_path;

            selected_entity->loadModel(selected_entity->model_path.c_str());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleVar();

    ImGui::Text("Scale:");
    ImGui::InputFloat("X##ScaleX", &entity_scale.x);
    ImGui::InputFloat("Y##ScaleY", &entity_scale.y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputFloat("Z##ScaleZ", &entity_scale.z);
    ImGui::PopStyleVar();
    selected_entity->scale = entity_scale;

    ImGui::Text("Position:");
    ImGui::InputFloat("X##PositionX", &entity_position.x);
    ImGui::InputFloat("Y##PositionY", &entity_position.y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputFloat("Z##PositionZ", &entity_position.z);
    ImGui::PopStyleVar();
    if (selected_entity->isChild)
        selected_entity->relative_position = entity_position;
    else
        selected_entity->position = entity_position;

    ImGui::Text("Color: ");
    ImGui::ColorEdit4("##Changeentity_color", (float*)&entity_colorImGui, ImGuiColorEditFlags_NoInputs);
    Color entity_color = (Color){ (unsigned char)(entity_colorImGui.x*255), (unsigned char)(entity_colorImGui.y*255), (unsigned char)(entity_colorImGui.z*255), (unsigned char)(color.w*255) };
    selected_entity->color = entity_color;

    ImGui::Text("Physics: ");
    ImGui::Text("Do Physics ");
    ImGui::SameLine();
    ImGui::Checkbox("##", &do_physics);




    ImGui::Text("Scripts: ");
    string name = string("Drop Script Here: ");
    ImGui::Text(name.c_str());
    ImGui::SameLine();
    string script_button_text = selected_entity_script_path + "##Drag'nDropScriptPath";
    if (ImGui::Button(script_button_text.c_str(), ImVec2(200,25)))
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

            selected_entity_script_path = path;
            selected_entity_script_path_index = 0;
            selected_entity->script = selected_entity_script_path;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Text("In Game Proprieties");
    ImGui::Text("Collider: ");
    ImGui::SameLine();
    ImGui::Checkbox("##Collider", &selected_entity->collider);

    ImGui::Text("Visible: ");
    ImGui::SameLine();
    ImGui::Checkbox("##Visible", &selected_entity->visible);


    ImGui::EndChild();
}

void LightInspector()
{
    ImGui::Text("Light Inspector ##Title");
}

void Inspector()
{
    if (selected_gameObject_type == "entity")
        EntityInspector();
    else
        LightInspector();
}


