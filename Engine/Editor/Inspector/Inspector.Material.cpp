#include "../../include_all.h"





void MaterialInspector(SurfaceMaterial* surface_material = nullptr, string path = "")
{
    SurfaceMaterial* material = nullptr;
    if (surface_material != nullptr)
        material = surface_material;

    
    if (path.empty())
        path = selected_material.string();

    DeserializeMaterial(material, path.c_str());
    if (!material){
        SurfaceMaterial emptySurfaceMaterial;
        material = &emptySurfaceMaterial;
    }

    ImGui::Text("Color: ");
    ImVec4 material_color(material->color.r, material->color.g, material->color.b, material->color.a);
    if (ImGui::ColorEdit4("##MaterialColor", (float*)&material_color, ImGuiColorEditFlags_NoInputs))
    {
        material->color = {
            material_color.x,
            material_color.y,
            material_color.z,
            material_color.w
        };
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));


    if (ImGui::CollapsingHeader("Maps")) {
        ImGui::Indent(10);
        
        float tiling_value[2] = { selected_entity->tiling[0], selected_entity->tiling[1] };
        ImGui::Text("Tiling: ");
        if (ImGui::SliderFloat("##TextureTiling0", (float*)&tiling_value[0], 0.01f, 100.0f, "%.1f"))
        {
            selected_entity->tiling[0] = tiling_value[0];
        }
        if (ImGui::SliderFloat("##TextureTiling1", (float*)&tiling_value[1], 0.01f, 100.0f, "%.1f"))
        {
            selected_entity->tiling[1] = tiling_value[1];
        }
        
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        
        ImGui::Text("Diffuse Texture: ");
        
        if (ImGui::ImageButton((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, ImVec2(64, 64)))
        {
            show_texture = !show_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                Texture2D diffuse_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(diffuse_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->texture_path = path;
                    selected_entity->texture = std::make_unique<VideoPlayer>(selected_entity->texture_path.string().c_str());
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

        ImGui::SameLine();
        if (ImGui::Button("x##DiffuseEmptyButton", ImVec2(25, 25)))
        {
            selected_entity->texture = Texture{};
            selected_entity->texture_path = "";
            selected_entity->ReloadTextures(true);
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));


        ImGui::Text("Normal Map Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture, ImVec2(64, 64)))
        {
            show_normal_texture = !show_normal_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                Texture2D normal_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(normal_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->normal_texture_path = path;
                    selected_entity->normal_texture = std::make_unique<VideoPlayer>(selected_entity->normal_texture_path.string().c_str());
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

        ImGui::SameLine();
        
        if (ImGui::Button("x##NormalEmptyButton", ImVec2(25, 25)))
        {
            selected_entity->normal_texture = Texture{};
            selected_entity->normal_texture_path = "";
            selected_entity->ReloadTextures(true);
        }


        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("RoughnessMap Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture, ImVec2(64, 64)))
        {
            show_roughness_texture = !show_roughness_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity->roughness_texture_path = path;
                Texture2D roughness_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(roughness_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->roughness_texture = std::make_unique<VideoPlayer>(selected_entity->roughness_texture_path.string().c_str());
                }
                else
                {
                    selected_entity->roughness_texture = roughness_texture;
                }

                selected_entity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("x##RoughnessEmptyButton", ImVec2(25, 25)))
        {
            selected_entity->roughness_texture = Texture{};
            selected_entity->roughness_texture_path = "";
            selected_entity->ReloadTextures(true);
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Ambient Occlusion Texture: ");
        if (ImGui::ImageButton((ImTextureID)&selected_entity->model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture, ImVec2(64, 64)))
        {
            show_ao_texture = !show_ao_texture;
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD"))
            {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                string path = dir_path.string();
                path += "/" + files_texture_struct[payload_n].name;

                selected_entity->ao_texture_path = path;
                Texture2D ao_texture = LoadTexture(path.c_str());
                if (!IsTextureReady(ao_texture)) // Means it is a video or an unsupported format
                {
                    selected_entity->ao_texture = std::make_unique<VideoPlayer>(selected_entity->ao_texture_path.string().c_str());
                }
                else
                {
                    selected_entity->ao_texture = ao_texture;
                }

                selected_entity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("x##AOEmptyButton", ImVec2(25, 25)))
        {
            selected_entity->ao_texture = Texture{};
            selected_entity->ao_texture_path = "";
            selected_entity->ReloadTextures(true);
        }


        ImGui::Unindent(10);
    }


    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::SliderFloat("Shininess", &material->shininess, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular Intensity", &material->SpecularIntensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &material->Roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse Intensity", &material->DiffuseIntensity, 0.0f, 1.0f);

    if (ImGui::Button("View Material in Nodes Editor"))
        show_material_in_nodes_editor = !show_material_in_nodes_editor;

    MaterialsNodeEditor(*material);


    SerializeMaterial(*material, path.c_str());
}