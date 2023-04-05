#include "../../include_all.h"


void EntityInspector()
{
    Entity *entity_in_inspector = get<Entity*>(object_in_inspector);

    entity_position = entity_in_inspector->position;
    if (entity_in_inspector->isChildren)
        entity_position = entity_in_inspector->relative_position;

    entity_scale = entity_in_inspector->scale;
    entity_color = entity_in_inspector->color;
    ImVec4 entity_colorImGui = ImVec4(
        entity_color.r / 255.0f,
        entity_color.g / 255.0f,
        entity_color.b / 255.0f,
        entity_color.a / 255.0f
    );

    ImVec2 windowSize = ImGui::GetWindowSize();
    string entity_name = "None";
    if (!entities_list_pregame.empty()) {
        entity_name = entity_in_inspector->name;
    }

    // Display window title
    stringstream title;
    title << "Inspecting '" << entity_name << "'";
    ImGui::Text(title.str().c_str());

    ImGui::BeginChild("MainContent", windowSize);


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
    string model_path_title = entity_in_inspector_model_path + "##Drag'nDropModelPath";

    ImGui::Text("Drop Model Here: ");
    ImGui::SameLine();
    if (ImGui::Button(model_path_title.c_str(), ImVec2(200,25)))
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

            entity_in_inspector_model_path = path;
            entity_in_inspector_model_path_index = 0;
            entity_in_inspector->model_path = entity_in_inspector_model_path;

            entity_in_inspector->loadModel(entity_in_inspector_model_path.c_str());
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
    entity_in_inspector->scale = entity_scale;

    ImGui::Text("Position:");

    ImGui::InputFloat("X##PositionX", &entity_position.x);
    ImGui::InputFloat("Y##PositionY", &entity_position.y);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::InputFloat("Z##PositionZ", &entity_position.z);
    ImGui::PopStyleVar();
    if (entity_in_inspector->isChildren)
    {
        entity_in_inspector->relative_position = entity_position;
    }
    else
    {
        entity_in_inspector->position = entity_position;
    }


    ImGui::Text("Color: ");
    ImGui::ColorEdit4("##Changeentity_color", (float*)&entity_colorImGui, ImGuiColorEditFlags_NoInputs);
    Color entity_color = (Color){ (unsigned char)(entity_colorImGui.x*255), (unsigned char)(entity_colorImGui.y*255), (unsigned char)(entity_colorImGui.z*255), (unsigned char)(color.w*255) };
    entity_in_inspector->color = entity_color;

    ImGui::Text("Physics: ");
    ImGui::Text("Do Physics ");
    ImGui::SameLine();
    ImGui::Checkbox("##", &do_physics);




    ImGui::Text("Scripts: ");
    string name = string("Drop Script Here: ") + entity_in_inspector_script_path;
    ImGui::Text(name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("##Drag'n Drop", ImVec2(200,25)))
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

            entity_in_inspector_script_path = path;
            entity_in_inspector_script_path_index = 0;
            entity_in_inspector->script = entity_in_inspector_script_path;
        }
        ImGui::EndDragDropTarget();
    }

    ImGui::Text("In Game Proprieties");
    ImGui::Text("Collider: ");
    ImGui::SameLine();
    ImGui::Checkbox("##Collider", &entity_in_inspector->collider);

    ImGui::Text("Visible: ");
    ImGui::SameLine();
    ImGui::Checkbox("##Visible", &entity_in_inspector->visible);


    ImGui::EndChild();
}

void LightInspector()
{
    ImGui::Text("Light Inspector##Title");
}

void Inspector()
{
    if (holds_alternative<Entity*>(object_in_inspector))
        EntityInspector();
    else
        LightInspector();
}


