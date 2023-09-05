#include "../../include_all.h"


void EntityInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();

    selected_entity = get<Entity*>(object_in_inspector);

    selected_entity_position = selected_entity->position;
    if (selected_entity->isChild)
        selected_entity_position = selected_entity->relative_position;

    selected_entity_scale = selected_entity->scale;
    selected_entity_color = selected_entity->color;
    ImVec4 entity_colorImGui = ImVec4(
        selected_entity_color.r / 255.0f,
        selected_entity_color.g / 255.0f,
        selected_entity_color.b / 255.0f,
        selected_entity_color.a / 255.0f
    );

    string entity_name;


        
    if (!entities_list_pregame.empty() && selected_entity == nullptr && selected_entity && !selected_entity->name.empty()) {
        try {
            selected_entity_name = selected_entity->name;
        } catch (std::bad_alloc) {
            return;
        }
    }
    else
    {
        selected_entity_name = "Unnamed Entity";
    }


    ImGui::Text("Inspecting '");
    ImGui::SameLine();

    float textWidth = ImGui::CalcTextSize(entity_name.c_str()).x + 10.0f;
    textWidth = std::max(textWidth, 100.0f);
    ImGui::SetNextItemWidth(textWidth);

    int name_size = 0;
    
    if (selected_entity->name.empty())
        name_size = 10;
    else
        name_size = selected_entity->name.size();
    
    char inputBuffer[255];
    strcpy(inputBuffer, selected_entity_name.c_str());

    if (ImGui::InputText("##Title Part 2", inputBuffer, sizeof(inputBuffer)))
        selected_entity->name = inputBuffer;



    ImGui::SameLine();
    ImGui::Text("'");


    ImGui::BeginChild("MainContent", window_size);


    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();


    ImGui::Text("Model Path:");
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
    ImGui::Text("Drop Model Here: ");
    ImGui::SameLine();

    if (ImGui::Button(
        ("##Drag'nDropModelPath"),
        ImVec2(200, 25)
        ));

    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODEL_PAYLOAD"))
        {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            string path = dir_path.c_str();
            path += "/" + files_texture_struct[payload_n].name;

            selected_entity->model_path = path;
            selected_entity_model_path_index = 0;
            selected_entity->model_path = selected_entity->model_path;

            selected_entity->setModel(selected_entity->model_path.c_str());
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopStyleVar();

    if (ImGui::CollapsingHeader("Entity Properties"))
    {
        ImGui::Text("Scale:");
        ImGui::InputFloat("X##ScaleX", &selected_entity_scale.x);
        ImGui::InputFloat("Y##ScaleY", &selected_entity_scale.y);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
        ImGui::InputFloat("Z##ScaleZ", &selected_entity_scale.z);
        ImGui::PopStyleVar();
        selected_entity->scale = selected_entity_scale;

        ImGui::Text("Position:");
        ImGui::InputFloat("X##PositionX", &selected_entity_position.x);
        ImGui::InputFloat("Y##PositionY", &selected_entity_position.y);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
        ImGui::InputFloat("Z##PositionZ", &selected_entity_position.z);
        ImGui::PopStyleVar();


        if (selected_entity->isChild)
        {
            selected_entity->relative_position = selected_entity_position;
        }
        else
        {
            if (selected_entity->isDynamic && selected_entity->calc_physics)
                selected_entity->applyForce(selected_entity_position);
            else
                selected_entity->position = selected_entity_position;
        }

        ImGui::Text("Rotation:");
        ImGui::SliderFloat("X##RotationX", &selected_entity->rotation.x, -360.0f, 360.0f);
        ImGui::SliderFloat("Y##RotationY", &selected_entity->rotation.y, -360.0f, 360.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
        ImGui::SliderFloat("Z##RotationZ", &selected_entity->rotation.z, -360.0f, 360.0f);
        ImGui::PopStyleVar();
    }

    if (ImGui::CollapsingHeader("Materials"))
    {
        ImGui::Text("Color: ");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
        ImGui::ColorEdit4("##Changeentity_color", (float*)&entity_colorImGui, ImGuiColorEditFlags_NoInputs);
        ImGui::PopStyleVar();
        Color entity_color = (Color){ (unsigned char)(entity_colorImGui.x*255), (unsigned char)(entity_colorImGui.y*255), (unsigned char)(entity_colorImGui.z*255), (unsigned char)(entity_colorImGui.w*255) };
        selected_entity->color = entity_color;

        ImGui::Text("Diffuse Texture: ");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));

        if (ImGui::ImageButton((ImTextureID)&image_texture, ImVec2(64, 64)))
        {
            show_texture = !show_texture;
        }

        ImGui::PopStyleVar();

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.c_str();
                path += "/" + files_texture_struct[payload_n].name;

                Texture2D diffuse_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(diffuse_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->texture_path = path;
                    selected_entity->texture = std::make_unique<VideoPlayer>(selected_entity->texture_path.c_str());
                }
                else
                {
                    selected_entity->texture_path = path;
                    selected_entity->texture = diffuse_texture;
                }

                selected_entity->ReloadTextures();
            }
            ImGui::EndDragDropTarget();
        }



        ImGui::Text("Normal Map Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->normal_texture, ImVec2(64, 64)))
        {
            show_texture = !show_normal_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.c_str();
                path += "/" + files_texture_struct[payload_n].name;

                Texture2D normal_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(normal_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->normal_texture_path = path;
                    selected_entity->normal_texture = std::make_unique<VideoPlayer>(selected_entity->normal_texture_path.c_str());
                }
                else
                {
                    selected_entity->normal_texture_path = path;
                    selected_entity->normal_texture = normal_texture;
                }
                selected_entity->ReloadTextures();
            }
            ImGui::EndDragDropTarget();
        }



        ImGui::Text("RoughnessMap Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->roughness_texture, ImVec2(64, 64)))
        {
            //show_texture = !show_normal_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.c_str();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity->roughness_texture = LoadTexture(path.c_str());
                selected_entity->roughness_texture_path = path;

                selected_entity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }


        ImGui::Text("Ambient Occlusion Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->ao_texture, ImVec2(64, 64)))
        {
            //show_texture = !show_normal_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.c_str();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity->ao_texture = LoadTexture(path.c_str());
                selected_entity->ao_texture_path = path;

                selected_entity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }

        if (ImGui::Button("View Material in Nodes Editor"))
            show_material_in_nodes_editor = !show_material_in_nodes_editor;

        
            

    }



    if (ImGui::CollapsingHeader("Physics"))
    {
        ImGui::Text("Is Dynamic ");
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 40));
        if (ImGui::Checkbox("##doPhysics", &selected_entity->isDynamic))
        {
            if (selected_entity->isDynamic)
                selected_entity->makePhysicsDynamic();

            else if (!selected_entity->isDynamic)
                selected_entity->makePhysicsStatic();

        }
        ImGui::PopStyleVar();

        ImGui::Text("Mass: ");
        ImGui::SameLine();
        ImGui::SliderFloat("##Mass", &selected_entity->mass, 0.0f, 1000.0f);
    }



    ImGui::Text("Scripts: ");
    ImGui::Text("Drop Script Here: ");
    ImGui::SameLine();

    if (ImGui::Button("##Drag'nDropScriptPath", ImVec2(200,25)))
    {
    }


    if (ImGui::BeginDragDropTarget())
    {
        // Check if a drag and drop operation has been accepted
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD"))
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