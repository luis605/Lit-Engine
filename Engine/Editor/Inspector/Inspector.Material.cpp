#include "../../include_all.h"


void SerializeMaterial(SurfaceMaterial& material, const char* path)
{
    json j;
    j["color"] = material.color;
    j["diffuse_texture_path"] = material.diffuse_texture_path;
    j["specular_texture_path"] = material.specular_texture_path;
    j["normal_texture_path"] = material.normal_texture_path;
    j["roughness_texture_path"] = material.roughness_texture_path;
    j["ao_texture_path"] = material.ao_texture_path;
    j["shininess"] = material.shininess;
    j["specular_intensity"] = material.SpecularIntensity;
    j["roughness"] = material.Roughness;
    j["diffuse_intensity"] = material.DiffuseIntensity;

    std::ofstream outfile(path);
    if (!outfile.is_open()) {
        std::cerr << "Error: Failed to open material file: " << path << std::endl;
        return 1;
    }

    outfile << std::setw(4) << j;
    outfile.close();
}

void DeserializeMaterial(SurfaceMaterial& material, const char* path) {
    // Attempt to open the JSON file for reading
    std::ifstream infile(path);
    if (!infile.is_open()) {
        std::cerr << "Error: Failed to open material file for reading: " << path << std::endl;
        return;
    }

    try {
        // Parse the JSON data from the file
        json j;
        infile >> j;

        // Check if the JSON object contains the expected material properties
        if (j.contains("color")) {
            material.color = j["color"];
        }
        if (j.contains("diffuse_texture_path")) {
            material.diffuse_texture_path = j["diffuse_texture_path"].get<fs::path>();
        }
        if (j.contains("specular_texture_path")) {
            material.specular_texture_path = j["specular_texture_path"].get<fs::path>();
        }
        if (j.contains("normal_texture_path")) {
            material.normal_texture_path = j["normal_texture_path"].get<fs::path>();
        }
        if (j.contains("roughness_texture_path")) {
            material.roughness_texture_path = j["roughness_texture_path"].get<fs::path>();
        }
        if (j.contains("ao_texture_path")) {
            material.ao_texture_path = j["ao_texture_path"].get<fs::path>();
        }
        if (j.contains("shininess")) {
            material.shininess = j["shininess"];
        }
        if (j.contains("specular_intensity")) {
            material.SpecularIntensity = j["specular_intensity"];
        }
        if (j.contains("roughness")) {
            material.Roughness = j["roughness"];
        }
        if (j.contains("diffuse_intensity")) {
            material.DiffuseIntensity = j["diffuse_intensity"];
        }

        // Close the input file
        infile.close();
    } catch (const json::parse_error& e) {
        std::cerr << "Error: Failed to parse material file (" << path << "): " << e.what() << std::endl;
    }
}


void MaterialInspector(SurfaceMaterial* surface_material = nullptr, string path = "")
{
    SurfaceMaterial* material;
    if (surface_material != nullptr)
        material = surface_material;
    
    if (path.empty())
        path = selected_material;

    DeserializeMaterial(*material, path.c_str());
    ImVec2 window_size = ImGui::GetWindowSize();
    
    ImGui::Text("Inspecting Material");    
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::BeginChild("MainContent", window_size);

    ImGui::Text("Color: ");
    // ImGui::ColorEdit4("##Changeentity_color", (float*)&entity_colorImGui, ImGuiColorEditFlags_NoInputs);
    // ImGui::PopStyleVar();
    // Color entity_color = (Color){ (unsigned char)(entity_colorImGui.x*255), (unsigned char)(entity_colorImGui.y*255), (unsigned char)(entity_colorImGui.z*255), (unsigned char)(entity_colorImGui.w*255) };
    // selected_entity->color = entity_color;

    ImGui::Dummy(ImVec2(0.0f, 10.0f));


    if (ImGui::CollapsingHeader("Maps")) {
        ImGui::Indent(10);
        ImGui::Text("Diffuse Texture: ");
        
        if (ImGui::ImageButton((ImTextureID)&image_texture, ImVec2(64, 64)))
        {
            show_texture = !show_texture;
        }

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


        ImGui::Dummy(ImVec2(0.0f, 10.0f));

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

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

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

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

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
        ImGui::Unindent(10);
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::SliderFloat("Shininess", &material->shininess, 0.0f, 1.0f);
    ImGui::SliderFloat("Specular Intensity", &material->SpecularIntensity, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &material->Roughness, 0.0f, 1.0f);
    ImGui::SliderFloat("Diffuse Intensity", &material->DiffuseIntensity, 0.0f, 1.0f);

    if (ImGui::Button("View Material in Nodes Editor"))
        show_material_in_nodes_editor = !show_material_in_nodes_editor;


    SerializeMaterial(*material, path.c_str());
        

    ImGui::EndChild();
}