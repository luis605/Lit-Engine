#include "EntitiesList.h"

void ManipulateEntityPopup() {
    if (!showManipulateEntityPopup) return;
    
    ImGui::OpenPopup("Entity");

    if (ImGui::BeginPopup("Entity")) {
        ImVec2 buttonScale = ImGui::CalcTextSize("Duplicate Entity") + ImVec2(20.0f, 20.0f);

        if (ImGui::Button("Duplicate Entity", buttonScale)) {
            DuplicateEntity(*selectedEntity);
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Copy Entity", buttonScale)) {
            currentCopyType = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selectedEntity);
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Delete Entity", buttonScale)) {
            entitiesListPregame.erase(std::remove(entitiesListPregame.begin(), entitiesListPregame.end(), *selectedEntity), entitiesListPregame.end());
            selectedEntity = nullptr;
            selectedGameObjectType = "";
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Locate Entity", buttonScale)) {
            LocateEntity(*selectedEntity);
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Rename Entity", buttonScale)) {
            shouldChangeObjectName = true;
            showManipulateEntityPopup = false;
        }
        
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        if (ImGui::Button("Close", buttonScale - ImVec2(0, 10))) showManipulateEntityPopup = false;
        ImGui::PopStyleColor();

        ImGui::EndPopup();
    }
}

bool DrawNodeTree(const char* icon, const std::string& name, ImGuiTreeNodeFlags flags, void* ptr, bool isSelected, const std::function<void()>& callback, bool* rightClicked = nullptr) {
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    bool isNodeOpen = ImGui::TreeNodeEx((std::string(icon) + " " + name + " ##" + std::to_string(entitiesListTreeNodeIndex)).c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) callback();
    if (rightClicked && ImGui::IsItemClicked(ImGuiMouseButton_Right)) *rightClicked = true;

    entitiesListTreeNodeIndex += 1;

    return isNodeOpen;
}
bool DrawTreeNodeWithRename(const char* icon, std::string& name, void* ptr, ImGuiTreeNodeFlags flags, bool isSelected, const std::function<void()>& callback, bool* rightClicked = nullptr) {
    if (isSelected && shouldChangeObjectName) {
        char nameBuffer[256];
        std::strncpy(nameBuffer, name.c_str(), sizeof(nameBuffer) - 1);
        if (ImGui::InputText("##Name", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            name = std::string(nameBuffer);
            shouldChangeObjectName = false;
        }
    }

    return DrawNodeTree(icon, name, flags, ptr, isSelected, callback, rightClicked);
}

void DrawEntityTree(Entity& entity) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    bool isSelected = (selectedEntity == &entity && selectedGameObjectType == "entity");
    bool rightClicked = false;

    bool isNodeOpen = DrawTreeNodeWithRename(ICON_FA_CUBE, entity.name, (void*)&entity, nodeFlags, isSelected, [&]() {
        selectedEntity = &entity;
        selectedGameObjectType = "entity";
    }, &rightClicked);

    if (rightClicked) showManipulateEntityPopup = true;

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        draggingChildObject = entity.isChild;

        ImGui::SetDragDropPayload("CHILD_ENTITY_PAYLOAD", &entity, sizeof(Entity));
        ImGui::EndDragDropSource();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_LIGHT_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(LightStruct)) {
                std::cerr << "Invalid payload size!" << std::endl;
                return;
            }

            LightStruct droppedLight = *static_cast<LightStruct*>(payload->Data);

            auto it = std::find_if(lights.begin(), lights.end(), [&](const LightStruct& lightStruct) { return lightStruct.id == droppedLight.id; });
            if (it != lights.end()) {
                std::cout << "Light found!" << std::endl;
                entity.addLightChild(findIndexInVector(lights, *it));
            } else {
                std::cerr << "Light not found!" << std::endl;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(Entity)) {
                std::cerr << "Invalid payload size!" << std::endl;
                return;
            }

            Entity droppedEntity = *static_cast<Entity*>(payload->Data);

            auto it = std::find_if(entitiesListPregame.begin(), entitiesListPregame.end(), [&](const Entity& entity) { return entity.id == droppedEntity.id; });
            if (it != entitiesListPregame.end()) {
                std::cout << "Entity found!" << std::endl;
                entity.addEntityChild(findIndexInVector(entitiesListPregame, *it));
            } else {
                std::cerr << "Entity not found!" << std::endl;
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (isNodeOpen) {
        for (int entityChildIndex : entity.entitiesChildren) {
            Entity& entityChild = entitiesListPregame[entityChildIndex];
            if (!entityChild.initialized) {
                ImGui::TreeNodeEx("ERROR: Entity child not initialized!", ImGuiTreeNodeFlags_NoTreePushOnOpen);
                continue;
            }

            DrawEntityTree(entityChild);
        }

        for (int lightStructIndex : entity.lightsChildren) {
            LightStruct& lightStruct = lights[lightStructIndex];
            DrawLightTree(lightStruct);
        }

        ImGui::TreePop();
    }
}

void DrawLightTree(LightStruct& lightStruct) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    bool isSelected = (selectedLight == &lightStruct && selectedGameObjectType == "light");

    bool isNodeOpen = DrawTreeNodeWithRename(ICON_FA_LIGHTBULB, lightStruct.lightInfo.name, (void*)&lightStruct, nodeFlags, isSelected, [&]() {
        selectedLight = &lightStruct;
        selectedGameObjectType = "light";
    });

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        draggingChildObject = lightStruct.isChild;
        ImGui::SetDragDropPayload("CHILD_LIGHT_PAYLOAD", &lightStruct, sizeof(LightStruct));
        ImGui::EndDragDropSource();
    }

    if (isNodeOpen) ImGui::TreePop();
}

void DrawTextElementsTree(Text& text) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    bool isSelected = (selectedTextElement == &text && selectedGameObjectType == "text");

    bool isNodeOpen = DrawTreeNodeWithRename(ICON_FA_QUOTE_LEFT, text.name, (void*)&text, nodeFlags, isSelected, [&]() {
        selectedTextElement = &text;
        selectedGameObjectType = "text";
    });

    if (isNodeOpen) ImGui::TreePop();
}

void DrawButtonTree(LitButton& button) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    bool isSelected = (selectedButton == &button && selectedGameObjectType == "button");

    bool isNodeOpen = DrawTreeNodeWithRename(ICON_FA_SQUARE, button.name, (void*)&button, nodeFlags, isSelected, [&]() {
        selectedButton = &button;
        selectedGameObjectType = "button";
    });

    if (isNodeOpen) ImGui::TreePop();
}

void DrawCameraTree() {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    bool isSelected = (selectedGameObjectType == "camera");

    bool isNodeOpen = DrawNodeTree(ICON_FA_VIDEO, sceneCamera.name, nodeFlags, (void*)&sceneCamera, isSelected, [&]() {
        selectedGameObjectType = "camera";
    });

    if (isNodeOpen) ImGui::TreePop();
}

void UnchildObjects(ImVec2 childSize) {
    if (!draggingChildObject) return;

    ImVec2 padding = ImGui::GetStyle().WindowPadding;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 buttonSize = childSize - padding * 2.0f;

    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("Background", buttonSize);
    ImGui::SetCursorScreenPos(pos);

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_LIGHT_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(LightStruct)) {
                std::cerr << "Invalid payload size!" << std::endl;
                return;
            }

            LightStruct droppedLight = *static_cast<LightStruct*>(payload->Data);

            if (!droppedLight.isChild) {
                std::cerr << "Dropped light is not a child." << std::endl;
                return;
            }

            int index = findIndexInVector(lights, droppedLight);
            LightStruct& light = lights[index];

            if (light.parent && light.parent->initialized) {
                auto it = std::find(light.parent->lightsChildren.begin(), light.parent->lightsChildren.end(), index);

                if (it != light.parent->lightsChildren.end()) { 
                    light.parent->lightsChildren.erase(it); 
                } 
            }

            light.parent = nullptr;
            light.isChild = false;
        }

        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(Entity)) {
                std::cerr << "Invalid payload size!" << std::endl;
                return;
            }

            Entity droppedEntity = *static_cast<Entity*>(payload->Data);

            if (!droppedEntity.isChild) {
                std::cerr << "Dropped entity is not a child." << std::endl;
                return;
            }

            int index = findIndexInVector(entitiesListPregame, droppedEntity);
            Entity& entity = entitiesListPregame[index];

            if (entity.parent && entity.parent->initialized) {
                auto it = std::find(entity.parent->entitiesChildren.begin(), entity.parent->entitiesChildren.end(), index);

                if (it != entity.parent->entitiesChildren.end()) { 
                    entity.parent->entitiesChildren.erase(it); 
                } 
            }

            entity.parent = nullptr;
            entity.isChild = false;
        }
        ImGui::EndDragDropTarget();
    }
}

void ImGuiListViewEx() {
    ImVec2 childSize = ImVec2(
        ImGui::GetWindowSize().x - 30,
        ImGui::GetWindowSize().y - 150);

    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

    UnchildObjects(childSize);

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered() || ImGui::IsItemHovered()) && IsKeyDown(KEY_F2))
        shouldChangeObjectName = true;

    if (ImGui::IsWindowFocused() && IsKeyDown(KEY_ESCAPE))
        shouldChangeObjectName = false;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10, 10));

    entitiesListTreeNodeIndex = 0;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

    DrawCameraTree();

    for (Entity& entity : entitiesListPregame) {
        if (entity.isChild || !entity.initialized) continue;
        DrawEntityTree(entity);
    }

    for (LightStruct& lightStruct : lights) {
        if (lightStruct.isChild) continue;
        DrawLightTree(lightStruct);
    }

    for (Text& text : textElements) {
        DrawTextElementsTree(text);
    }

    for (LitButton& button : litButtons) {
        DrawButtonTree(button);
    }

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();
    ImGui::PopStyleColor(4);

    ImGui::EndChild();

    ManipulateEntityPopup();
}

void PlayPause() {
    ImVec2 buttonSize = ImVec2(50, 50);

    if (ImGui::ImageButton((ImTextureID)&runTexture, buttonSize) && !inGamePreview) {
        entitiesList.assign(entitiesListPregame.begin(), entitiesListPregame.end());
        
        physics.backup();

        InitGameCamera();
        inGamePreview = true;
    }

    ImGui::SameLine();

    if ((ImGui::ImageButton((ImTextureID)&pauseTexture, buttonSize)) && inGamePreview || IsKeyDown(KEY_ESCAPE)) {
        EnableCursor();

        inGamePreview = false;
        firstTimeGameplay = true;

        physics.unBackup();
        for (Entity& entity : entitiesListPregame) entity.resetPhysics();
        for (Entity& entity : entitiesList) entity.resetPhysics();
    }
}

void OpenWebpages() {
    if (inGamePreview || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;

    if (IsKeyPressed(KEY_F1)) openAboutPage();
    if (IsKeyPressed(KEY_F2)) openManualPage();
}

void EntitiesList() {
    ImGui::Begin((std::string(ICON_FA_BARS) + " Objects List").c_str());

    ImGuiListViewEx();

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    PlayPause();
    OpenWebpages();

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    ImGui::End();
}