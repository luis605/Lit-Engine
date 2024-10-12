#include "include_all.h"

#include "ObjectsList.h"

void ManipulateEntityPopup() {
    if (!showManipulateEntityPopup) return;

    ImGui::OpenPopup("Entity");

    if (ImGui::BeginPopup("Entity")) {
        static ImVec2 buttonScale = ImGui::CalcTextSize("Duplicate Entity") + ImVec2(20.0f, 20.0f);

        if (ImGui::Button("Duplicate Entity", buttonScale)) {
            DuplicateEntity(*selectedEntity);
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Copy Entity", buttonScale)) {
            currentCopyType = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selectedEntity);
            showManipulateEntityPopup = false;
        } else if (ImGui::Button("Delete Entity", buttonScale)) {
            removeEntity(selectedEntity->id);
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

bool DrawNodeTree(const char* icon, const std::string& name, ImGuiTreeNodeFlags flags, void* ptr, bool isSelected, const std::function<void()>& callback, bool* rightClicked) {
    if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

    bool isNodeOpen = ImGui::TreeNodeEx((std::string(icon) + " " + name + " ##" + std::to_string(entitiesListTreeNodeIndex)).c_str(), flags);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) callback();
    if (rightClicked && ImGui::IsItemClicked(ImGuiMouseButton_Right)) *rightClicked = true;

    entitiesListTreeNodeIndex++;
    return isNodeOpen;
}

bool DrawTreeNodeWithRename(const char* icon, std::string& name, void* ptr, ImGuiTreeNodeFlags flags, bool isSelected, const std::function<void()>& callback, bool* rightClicked) {
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
                TraceLog(LOG_WARNING, "Invalid payload size.");
                return;
            }

            LightStruct droppedLight = *static_cast<LightStruct*>(payload->Data);

            entity.addLightChild(droppedLight.id);
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(Entity)) {
                TraceLog(LOG_WARNING, "Invalid payload size.");
                return;
            }

            Entity droppedEntity = *static_cast<Entity*>(payload->Data);

            entity.addEntityChild(droppedEntity.id);
        }
        ImGui::EndDragDropTarget();
    }

    if (isNodeOpen) {
        for (int entityChildIndex : entity.entitiesChildren) {
            Entity* entityChild = getEntityById(entityChildIndex);
            if (!entityChild) {
                ImGui::TreeNodeEx("ERROR: Entity child not initialized!", ImGuiTreeNodeFlags_NoTreePushOnOpen);
                continue;
            }

            DrawEntityTree(*entityChild);
        }

        for (int lightStructIndex : entity.lightsChildren) {
            LightStruct* lightStruct = getLightById(lightStructIndex);
            if (!lightStruct) {
                ImGui::TreeNodeEx("ERROR: Light child not found!", ImGuiTreeNodeFlags_NoTreePushOnOpen);
                continue;
            }
            DrawLightTree(*lightStruct);
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
                TraceLog(LOG_WARNING, "Invalid payload size.");
                return;
            }

            LightStruct droppedLight = *static_cast<LightStruct*>(payload->Data);

            if (!droppedLight.isChild) {
                return;
            }

            LightStruct* light = getLightById(droppedLight.id);

            if (light->parent && light->parent->initialized) {
                auto it = std::find(light->parent->lightsChildren.begin(), light->parent->lightsChildren.end(), light->id);

                if (it != light->parent->lightsChildren.end()) {
                    light->parent->lightsChildren.erase(it);
                }
            }

            light->parent = nullptr;
            light->isChild = false;
        }

        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            if (payload->DataSize != sizeof(Entity)) {
                TraceLog(LOG_WARNING, "Invalid payload size.");
                return;
            }

            Entity droppedEntity = *static_cast<Entity*>(payload->Data);

            if (!droppedEntity.isChild) {
                return;
            }

            Entity* entity = getEntityById(droppedEntity.id);
            if (!entity) return;

            if (entity->parent && entity->parent->initialized) {
                auto it = std::find(entity->parent->entitiesChildren.begin(), entity->parent->entitiesChildren.end(), entity->id);

                if (it != entity->parent->entitiesChildren.end()) {
                    entity->parent->entitiesChildren.erase(it);
                }
            }

            entity->parent = nullptr;
            entity->isChild = false;
        }
        ImGui::EndDragDropTarget();
    }
}

void ImGuiListViewEx() {
    ImVec2 entitiesListWindowSize = ImGui::GetContentRegionAvail();
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
        ImGui::BeginChild("Entities List", entitiesListWindowSize);
    ImGui::PopStyleVar();

    UnchildObjects(entitiesListWindowSize);

    const bool windowFocused = ImGui::IsWindowFocused();
    const bool windowHovered = ImGui::IsWindowHovered();
    const bool itemHovered = ImGui::IsItemHovered();

    if ((windowFocused || windowHovered || itemHovered) && IsKeyDown(KEY_F2))
        shouldChangeObjectName = true;

    if (windowFocused && IsKeyDown(KEY_ESCAPE))
        shouldChangeObjectName = false;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10, 10));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

    ImGui::PushItemWidth(-1);

    entitiesListTreeNodeIndex = 0;

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

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(3);
    ImGui::PopItemWidth();

    ImGui::EndChild();

    ManipulateEntityPopup();
}

void OpenWebpages() {
    if (inGamePreview || ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) return;

    if (IsKeyPressed(KEY_F1)) openAboutPage();
    if (IsKeyPressed(KEY_F2)) openManualPage();
}

void EntitiesList() {
    ImGui::Begin((std::string(ICON_FA_BARS) + " Objects List").c_str());

    ImGuiListViewEx();
    OpenWebpages();

    ImGui::End();
}