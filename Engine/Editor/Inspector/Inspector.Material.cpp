void MaterialInspector(SurfaceMaterial* surfaceMaterial = nullptr, fs::path path = "")
{
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
        ImGui::Indent(10);

        float tiling_value[2];
        if (selectedEntity) {
            tiling_value[0] = selectedEntity->tiling[0];
            tiling_value[1] = selectedEntity->tiling[1];
        } else {
            tiling_value[0] = 1.0f;
            tiling_value[1] = 1.0f;
        }

        ImGui::Text("Tiling: ");

        ImGui::Indent(20.0f);
        
        ImGui::Text("X:");
        ImGui::SameLine();
        
        if (ImGui::SliderFloat("##TextureTiling0", (float*)&tiling_value[0], 0.01f, 100.0f, "%.1f"))
            selectedEntity->tiling[0] = tiling_value[0];
        
        ImGui::Text("Y:");
        ImGui::SameLine();
        
        if (ImGui::SliderFloat("##TextureTiling1", (float*)&tiling_value[1], 0.01f, 100.0f, "%.1f"))
            selectedEntity->tiling[1] = tiling_value[1];
        
        ImGui::Unindent(20.0f);
        
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        
        ImGui::Text("Diffuse Texture: ");

        ImGui::Indent(20.0f);

        if (ImGui::ImageButton((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture, ImVec2(64, 64)))
            showTexture = !showTexture;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

            if (payload) {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                fs::path path = dirPath;
                path += "/" + fileStruct[payload_n].name;

                surfaceMaterial->diffuseTexturePath = path;
                surfaceMaterial->diffuseTexture = path;
            }

            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("x##DiffuseEmptyButton", ImVec2(25, 25))) {
            surfaceMaterial->diffuseTexture.cleanup();
            surfaceMaterial->diffuseTexturePath = "";
            selectedEntity->ReloadTextures(true);
        }

        ImGui::Unindent(20.0f);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Normal Map Texture: ");
        
        ImGui::Indent(20.0f);

        if (ImGui::ImageButton((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_NORMAL].texture, ImVec2(64, 64)))
            showNormalTexture = !showNormalTexture;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

            if (payload) {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                fs::path path = dirPath;
                path += "/" + fileStruct[payload_n].name;

                surfaceMaterial->normalTexturePath = path;
                surfaceMaterial->normalTexture = path;

                selectedEntity->ReloadTextures();
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        
        if (ImGui::Button("x##NormalEmptyButton", ImVec2(25, 25))) {
            surfaceMaterial->normalTexture.cleanup();
            surfaceMaterial->normalTexturePath = "";
            selectedEntity->ReloadTextures(true);
        }

        ImGui::Unindent(20.0f);

        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("RoughnessMap Texture: ");

        ImGui::Indent(20.0f);

        if (ImGui::ImageButton((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_ROUGHNESS].texture, ImVec2(64, 64)))
            showRoughnessTexture = !showRoughnessTexture;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

            if (payload) {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                fs::path path = dirPath;
                path += "/" + fileStruct[payload_n].name;

                surfaceMaterial->roughnessTexture = path;
                surfaceMaterial->roughnessTexturePath = path;

                selectedEntity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("x##RoughnessEmptyButton", ImVec2(25, 25))) {
            surfaceMaterial->roughnessTexture.cleanup();
            surfaceMaterial->roughnessTexturePath = "";
            selectedEntity->ReloadTextures(true);
        }

        ImGui::Unindent(20.0f);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        ImGui::Text("Ambient Occlusion Texture: ");

        ImGui::Indent(20.0f);

        if (ImGui::ImageButton((ImTextureID)&selectedEntity->model.materials[0].maps[MATERIAL_MAP_OCCLUSION].texture, ImVec2(64, 64)))
            showAOTexture = !showAOTexture;

        if (ImGui::BeginDragDropTarget()) {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("TEXTURE_PAYLOAD");

            if (payload) {
                IM_ASSERT(payload->DataSize == sizeof(int));
                int payload_n = *(const int*)payload->Data;

                fs::path path = dirPath;
                path += "/" + fileStruct[payload_n].name;

                surfaceMaterial->aoTexturePath = path;
                surfaceMaterial->aoTexture = path;

                selectedEntity->ReloadTextures();

            }
            ImGui::EndDragDropTarget();
        }

        ImGui::SameLine();
        if (ImGui::Button("x##AOEmptyButton", ImVec2(25, 25))) {
            surfaceMaterial->aoTexture.cleanup();
            surfaceMaterial->aoTexturePath = "";
            selectedEntity->ReloadTextures(true);
        }

        ImGui::Unindent(20.0f);
        ImGui::Unindent(10);
    }


    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    
    const float margin = 40.0f;
    const float spacing = ImGui::CalcTextSize("Specular Intensity:").x + margin;

    ImGui::Text("Specular Intensity:");
    ImGui::SameLine(spacing);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Specular Intensity", &surfaceMaterial->SpecularIntensity, 0.0f, 1.0f);

    ImGui::Text("Diffuse Intensity:");
    ImGui::SameLine(spacing);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Diffuse Intensity", &surfaceMaterial->DiffuseIntensity, 0.0f, 1.0f);

    ImGui::Text("Roughness:");
    ImGui::SameLine(spacing);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Roughness", &surfaceMaterial->Roughness, 0.0f, 1.0f);

    ImGui::Text("Metalness:");
    ImGui::SameLine(spacing);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Metalness", &surfaceMaterial->Metalness, 0.0f, 1.0f);

    if (ImGui::Button("View Material in Nodes Editor"))
        showMaterialInNodesEditor = !showMaterialInNodesEditor;

    MaterialsNodeEditor(*surfaceMaterial);


    SerializeMaterial(*surfaceMaterial, path.c_str());
}