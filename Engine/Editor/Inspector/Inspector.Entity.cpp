#include "../../../include_all.h"


void EntityInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();

    selected_entity = get<Entity*>(object_in_inspector);

    selected_entity_position = selected_entity->position;
    if (selected_entity->isChild)
        selected_entity_position = selected_entity->relative_position;

    selected_entity_scale = selected_entity->scale;

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

            string path = dir_path.string();
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

        ImGui::Text("Scripts: ");
        ImGui::Text("Drop Script Here: ");
        ImGui::SameLine();

        ImGui::Button("##Drag'nDropScriptPath", ImVec2(200,25));

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("SCRIPT_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity_script_path = path;
                selected_entity_script_path_index = 0;
                selected_entity->script = selected_entity_script_path;
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::Text("Collider: ");
        ImGui::SameLine();
        ImGui::Checkbox("##Collider", &selected_entity->collider);

        ImGui::Text("Visible: ");
        ImGui::SameLine();
        ImGui::Checkbox("##Visible", &selected_entity->visible);


    }





    if (ImGui::CollapsingHeader("Materials"))
    {
        ImGui::Indent(10);

        ImGui::Text("Drop Material Here: ");
        ImGui::SameLine();

        if (ImGui::Button(
            ("##Drag'nDropMaterialPath"),
            ImVec2(200, 25)
            ));

        if (ImGui::BeginDragDropTarget())
        {

            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MATERIAL_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity->surface_material_path = path;
                DeserializeMaterial(&selected_entity->surface_material, selected_entity->surface_material_path.string().c_str());
            }
            ImGui::EndDragDropTarget();
        }

        if (!selected_entity->surface_material_path.empty())
        {
            MaterialInspector(&selected_entity->surface_material, selected_entity->surface_material_path.string());
        }

        ImGui::Unindent(10);


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


        ImGui::Text("Collision Type");
        ImGui::SameLine();
        const char* collisionShapeNames[] = { "Box", "HighPolyMesh", "LowPolyMesh", "Sphere", "None" };
        int currentItem = static_cast<int>(*selected_entity->currentCollisionShapeType);
        if (ImGui::BeginCombo("##CollisionType", collisionShapeNames[currentItem]))
        {
            for (int i = 0; i < IM_ARRAYSIZE(collisionShapeNames); i++)
            {
                const bool isSelected = (currentItem == i);
                if (ImGui::Selectable(collisionShapeNames[i], isSelected))
                {
                    *selected_entity->currentCollisionShapeType = static_cast<Entity::CollisionShapeType>(i);
                    selected_entity->reloadRigidBody();
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }


        ImGui::Text("Mass: ");
        ImGui::SameLine();
        ImGui::SliderFloat("##Mass", &selected_entity->mass, 0.0f, 1000.0f);
    }




    if (ImGui::CollapsingHeader("Advanced Settings"))
    {
        ImGui::TextWrapped("Warning!\n"
"Some of these features are experimental and may not save or load from files!\n"
"Other settings may be complex for new users.\n"
"Lit Engine is a prototype and does not guarantee all features working|\n"
"We DO NOT recommend visiting the Advanced Settings section until we reach the alpha state!");

        if (ImGui::Button("Set all children instanced"))
        {
            selected_entity->makeChildrenInstances();
        }
    }

    ImGui::EndChild();
}