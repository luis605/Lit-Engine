void MapInspector(const char* title, const MaterialMapIndex& materialMapIndex, bool& showTexture, fs::path& texturePath, SurfaceMaterialTexture& texture) {
    ImGui::Text(title);

    ImGui::Indent();

    if (ImGui::ImageButton(title, (ImTextureID)&selectedEntity->model.materials[0].maps[materialMapIndex].texture, ImVec2(64, 64)))
        showTexture = !showTexture;

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

        if (payload) {
            IM_ASSERT(payload->DataSize == sizeof(int));
            int payload_n = *(const int*)payload->Data;

            fs::path path = dirPath / fileStruct[payload_n].name;

            texturePath = path;
            texture = path;
        }

        ImGui::EndDragDropTarget();
    }

    ImGui::SameLine();
    std::string emptyButtonName = std::string("x##" + std::string(title) + "EmptyButton").c_str();
    if (ImGui::Button(emptyButtonName.c_str(), ImVec2(25, 25))) {
        texture.cleanup();
        texturePath = "";
        selectedEntity->ReloadTextures(true);
    }

    ImGui::Unindent();
}

void MaterialInspector(SurfaceMaterial* surfaceMaterial = nullptr, fs::path path = "") {
    if (!selectedEntity) return;

    if (path.empty()) path = selectedMaterial;

    DeserializeMaterial(surfaceMaterial, path.c_str());

    if (!surfaceMaterial) {
        SurfaceMaterial emptyMaterial;
        surfaceMaterial = &emptyMaterial;
    };

    ImGui::Text("Color: ");
    ImVec4 material_color(surfaceMaterial->color.r, surfaceMaterial->color.g, surfaceMaterial->color.b, surfaceMaterial->color.a);
    if (ImGui::ColorEdit4("##MaterialColor", (float*)&material_color, ImGuiColorEditFlags_NoInputs)) {
        surfaceMaterial->color = {
            material_color.x,
            material_color.y,
            material_color.z,
            material_color.w
        };
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (ImGui::CollapsingHeader("Maps")) {
        ImGui::Indent();

        float tiling_value[2];
        if (selectedEntity) {
            tiling_value[0] = selectedEntity->tiling[0];
            tiling_value[1] = selectedEntity->tiling[1];
        } else {
            tiling_value[0] = 1.0f;
            tiling_value[1] = 1.0f;
        }

        ImGui::Text("Tiling: ");

        ImGui::Indent();

        ImGui::Text("X:");
        ImGui::SameLine();

        if (ImGui::SliderFloat("##TextureTiling0", (float*)&tiling_value[0], 0.01f, 100.0f, "%.1f"))
            selectedEntity->tiling[0] = tiling_value[0];

        ImGui::Text("Y:");
        ImGui::SameLine();

        if (ImGui::SliderFloat("##TextureTiling1", (float*)&tiling_value[1], 0.01f, 100.0f, "%.1f"))
            selectedEntity->tiling[1] = tiling_value[1];

        ImGui::SameLine();
        if (ImGui::Button("X")) {
            selectedEntity->tiling[0] = 1.0f;
            selectedEntity->tiling[1] = 1.0f;
        }

        ImGui::Unindent();

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Albedo Map: ", MATERIAL_MAP_ALBEDO, showAlbedoTexture, surfaceMaterial->albedoTexturePath, surfaceMaterial->albedoTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Normal Map: ", MATERIAL_MAP_NORMAL, showNormalTexture, surfaceMaterial->normalTexturePath, surfaceMaterial->normalTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Roughness Map: ", MATERIAL_MAP_ROUGHNESS, showRoughnessTexture, surfaceMaterial->roughnessTexturePath, surfaceMaterial->roughnessTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Ambient Occlusion Map: ", MATERIAL_MAP_OCCLUSION, showAOTexture, surfaceMaterial->aoTexturePath, surfaceMaterial->aoTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Height Map: ", MATERIAL_MAP_HEIGHT, showHeightTexture, surfaceMaterial->heightTexturePath, surfaceMaterial->heightTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Metallic Map: ", MATERIAL_MAP_METALNESS, showMetallicTexture, surfaceMaterial->metallicTexturePath, surfaceMaterial->metallicTexture);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
            MapInspector("Emissive Map: ", MATERIAL_MAP_EMISSION, showEmissiveTexture, surfaceMaterial->emissiveTexturePath, surfaceMaterial->emissiveTexture);

        ImGui::Unindent();
    }


    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    const float margin = 40.0f;
    static float spacing = ImGui::CalcTextSize("Specular Intensity:").x + margin;

    ImGui::Text("Specular Intensity:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Specular Intensity", &surfaceMaterial->specularIntensity, 0.0f, 1.0f);

    ImGui::Text("Diffuse Intensity:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Diffuse Intensity", &surfaceMaterial->albedoIntensity, 0.0f, 1.0f);

    ImGui::Text("Roughness:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Roughness", &surfaceMaterial->roughness, 0.0f, 1.0f);

    ImGui::Text("Metalness:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Metalness", &surfaceMaterial->metalness, 0.0f, 1.0f);

    ImGui::Text("Clear Coat:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ClearCoat", &surfaceMaterial->clearCoat, 0.0f, 1.0f);

    ImGui::Text("Clear Coat Roughness:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##ClearCoatRoughness", &surfaceMaterial->clearCoatRoughness, 0.0f, 1.0f);

    ImGui::Text("Subsurface:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Subsurface", &surfaceMaterial->subsurface, 0.0f, 1.0f);

    ImGui::Text("Anisotropy:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Anisotropy", &surfaceMaterial->anisotropy, 0.0f, 1.0f);

    ImGui::Text("Transmission:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Transmission", &surfaceMaterial->transmission, 0.0f, 1.0f);

    ImGui::Text("Ior:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Ior", &surfaceMaterial->ior, 0.0f, 1.0f);

    ImGui::Text("Thickness:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Thickness", &surfaceMaterial->thickness, 0.0f, 1.0f);

    ImGui::Text("Height Scale:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##HeightScale", &surfaceMaterial->heightScale, -12.0f, 12.0f);

    ImGui::Text("Emissive Intensity:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##EmmisiveIntensity", &surfaceMaterial->emissiveIntensity, 0.0f, 1.0f);

    ImGui::Text("AO Strength:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##AOStrength", &surfaceMaterial->aoStrength, 0.0f, 1.0f);

    if (ImGui::Button("View Material in Nodes Editor"))
        showMaterialInNodesEditor = !showMaterialInNodesEditor;

    MaterialsNodeEditor(*surfaceMaterial);


    SerializeMaterial(*surfaceMaterial, path.c_str());
}