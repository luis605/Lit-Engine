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

    if (ImGui::Button("View Material in Nodes Editor"))
        showMaterialInNodesEditor = !showMaterialInNodesEditor;
    showMaterialInNodesEditor = true;
    materialNodeSystem.DrawMaterialNodeEditor(*surfaceMaterial);


    SerializeMaterial(*surfaceMaterial, path.c_str());
}