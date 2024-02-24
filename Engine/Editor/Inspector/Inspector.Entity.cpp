#include "../../../include_all.h"


void EntityInspector()
{
    ImVec2 window_size = ImGui::GetWindowSize();

    selected_entity = get<Entity*>(object_in_inspector);

    if (selected_entity == nullptr || !selected_entity->initialized) return;

    selected_entity_position = selected_entity->position;
    if (selected_entity->isChild)
        selected_entity_position = selected_entity->relative_position;

    selected_entity_scale = selected_entity->scale;

    string entity_name;


        
    selected_entity_name = selected_entity->name;


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
    size_t buffer_size = sizeof(inputBuffer);
    strncpy(inputBuffer, selected_entity_name.c_str(), buffer_size - 1);
    inputBuffer[buffer_size - 1] = '\0';

    if (ImGui::InputText("##Title Part 2", inputBuffer, sizeof(inputBuffer)))
        selected_entity->name = inputBuffer;



    ImGui::SameLine();
    ImGui::Text("'");

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 30));
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (ImGui::CollapsingHeader("Entity Properties"))
    {
        ImGui::Indent(30.0f);

        ImGui::Text("Scale:");
        ImGui::Indent(20.0f);

        ImGui::Text("X:");
        ImGui::SameLine();
        ImGui::InputFloat("##ScaleX", &selected_entity_scale.x);
        
        ImGui::Text("Y:");
        ImGui::SameLine();
        ImGui::InputFloat("##ScaleY", &selected_entity_scale.y);
        
        ImGui::Text("Z:");
        ImGui::SameLine();
        ImGui::InputFloat("##ScaleZ", &selected_entity_scale.z);
        
        selected_entity->scale = selected_entity_scale;
            
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0.0f, 30.0f));

        ImGui::Text("Position:");
        ImGui::Indent(20.0f);

        ImGui::Text("X:");
        ImGui::SameLine();
        ImGui::InputFloat("##PositionX", &selected_entity_position.x);

        ImGui::Text("Y:");
        ImGui::SameLine();
        ImGui::InputFloat("##PositionY", &selected_entity_position.y);

        ImGui::Text("Z:");
        ImGui::SameLine();
        ImGui::InputFloat("##PositionZ", &selected_entity_position.z);

        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0.0f, 30.0f));

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
        ImGui::Indent(20.0f);

        ImGui::Text("X:");
        ImGui::SameLine();
        if (EntityRotationXInputModel)
        {
            if (ImGui::InputFloat("##RotationX", &selected_entity->rotation.x, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                EntityRotationXInputModel = false;
        }
        else
        {
            ImGui::SliderFloat("##RotationX", &selected_entity->rotation.x, -180.0f, 180.0f, "%.3f");
            EntityRotationXInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        ImGui::Text("Y:");
        ImGui::SameLine();
        if (EntityRotationYInputModel)
        {
            if (ImGui::InputFloat("##RotationY", &selected_entity->rotation.y, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                EntityRotationYInputModel = false;
        }
        else
        {
            ImGui::SliderFloat("##RotationY", &selected_entity->rotation.y, -180.0f, 180.0f, "%.3f");
            EntityRotationYInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }

        ImGui::Text("Z:");
        ImGui::SameLine();
        if (EntityRotationZInputModel)
        {
            if (ImGui::InputFloat("##RotationZ", &selected_entity->rotation.z, -180.0f, 180.0f, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
                EntityRotationZInputModel = false;
        }
        else
        {
            ImGui::SliderFloat("##RotationZ", &selected_entity->rotation.z, -180.0f, 180.0f, "%.3f");
            EntityRotationZInputModel = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0);
        }
        
        selected_entity->setRot(selected_entity->rotation);
        
        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0.0f, 30.0f));

        const float margin = 40.0f;
        const float LODWidth = ImGui::CalcTextSize("Level of Detail: ").x + margin;


        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 30));
        ImGui::Text("Model: ");
        ImGui::SameLine(LODWidth);

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


        ImGui::Text("Script: ");
        ImGui::SameLine(LODWidth);

        const char* script_name = selected_entity->script.c_str();
        if (selected_entity->script.empty()) script_name = "##ScriptPath";
        
        ImGui::Button(script_name, ImVec2(200,25));

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

        ImGui::SameLine();

        if (ImGui::Button("x##ScriptEmptyButton", ImVec2(25, 25)))
        {
            selected_entity->script = "";
            selected_entity->script_index = "NONE";
        }

        ImGui::Dummy(ImVec2(0.0f, 30.0f));

        ImGui::Text("Collisions: ");
        ImGui::SameLine(LODWidth);
        ImGui::Checkbox("##Collisions", &selected_entity->collider);

        ImGui::Text("Visible: ");
        ImGui::SameLine(LODWidth);
        ImGui::Checkbox("##Visible", &selected_entity->visible);

        ImGui::Text("Level of Detail: ");
        ImGui::SameLine(LODWidth);
        ImGui::Checkbox("##Lod", &selected_entity->lodEnabled);

        ImGui::Unindent(30.0f);
    }




    ImGui::Dummy(ImVec2(0.0f, 5.0f));




    if (ImGui::CollapsingHeader("Materials"))
    {
        ImGui::Indent(30.0f);

        ImGui::Text("Material:");
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

        if (!selected_entity->surface_material_path.empty() && selected_entity->surface_material_path.has_filename())
        {
            MaterialInspector(&selected_entity->surface_material, selected_entity->surface_material_path.string());
        }

        ImGui::Unindent(30.0f);


    }


    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    if (ImGui::CollapsingHeader("Physics"))
    {
        ImGui::Indent(30.0f);

        ImGui::Text("Collision Type");
        ImGui::SameLine();

        const char* collisionShapeNames[] = {"Box", "HighPolyMesh", "None"};
        int currentItem = static_cast<int>(Entity::CollisionShapeType::None);

        if (selected_entity->currentCollisionShapeType)
            currentItem = static_cast<int>(*selected_entity->currentCollisionShapeType);

        const float comboWidth = ImGui::GetContentRegionAvail().x - 30.0f;

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

        const float sliderWidth = comboWidth;
        const float marginLeft  = 30.0f;

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImGui::Text("Is Dynamic");
        ImGui::SameLine();

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (sliderWidth + marginLeft));

        if (ImGui::Checkbox("##doPhysics", &selected_entity->isDynamic))
        {
            if (selected_entity->isDynamic)
                selected_entity->makePhysicsDynamic();
            else
                selected_entity->makePhysicsStatic();
        }

        ImGui::Dummy(ImVec2(0.0f, 15.0f));

        ImGui::Text("Mass:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (sliderWidth + marginLeft));
        ImGui::SetNextItemWidth(sliderWidth);
        if (ImGui::SliderFloat("##Mass", &selected_entity->mass, 0.0f, 100.0f, "%.1f"))
        {
            selected_entity->updateMass();
        }

        ImGui::Text("Friction:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (sliderWidth + marginLeft));
        ImGui::SetNextItemWidth(sliderWidth);
        if (ImGui::SliderFloat("##Friction", &selected_entity->friction, 0.0f, 10.0f, "%.1f"))
        {
            selected_entity->setFriction(selected_entity->friction);
        }

        ImGui::Text("Damping:");
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - (sliderWidth + marginLeft));
        ImGui::SetNextItemWidth(sliderWidth);
        if (ImGui::SliderFloat("##Damping", &selected_entity->damping, 0.0f, 5.0f, "%.1f"))
        {
            selected_entity->applyDamping(selected_entity->damping);
        }

        ImGui::Unindent(30.0f);
    }



    ImGui::Dummy(ImVec2(0.0f, 5.0f));



    if (ImGui::CollapsingHeader("Advanced Settings"))
    {
        ImGui::Indent(30.0f);

        ImGui::TextWrapped("Warning!\n"
"These experimental features won't save or load.\n"
"Avoid Advanced Settings until the alpha state.");

        if (ImGui::Button("Set all children instanced"))
        {
            selected_entity->makeChildrenInstances();
        }

        ImGui::Unindent(30.0f);
    }
}