#include "../../../include_all.h"

bool shouldChangeObjectName = false;
bool showManipulateEntityPopup = false;

void DrawEntityTree(Entity& entity, int active, int& index, int depth = 0);
void DrawLightTree(Light& light, AdditionalLightInfo& light_info, int active, int& index);
void DrawTextElementsTree(Text& text, int active, int& index);
void DrawButtonTree(LitButton& button, int active, int& index);
void DrawCameraTree(int active, int& index);
void updateListViewExList(vector<Entity>& entities, vector<Light>& lights);
void ManipulateEntityPopup();

void updateListViewExList(vector<Entity>& entities, vector<Light>& lights) {
    listViewExList.clear();
    objectNames.clear();
    listViewExListTypes.clear();

    for (int index = 0; index < entities.size() + lights.size(); index++) {
        string name;
        if (index < entities.size()) {
            name = entities[index].name;
            listViewExListTypes.push_back("entity");
        } else {
            name = lightsInfo[index - entities.size()].name;
            listViewExListTypes.push_back("light");
        }
        objectNames.push_back(name);
    }


    for (int i = 0; i < objectNames.size(); i++) {
        listViewExList.push_back((char*)objectNames[i].c_str());
    }
}

void ManipulateEntityPopup()
{
    if (showManipulateEntityPopup)
        ImGui::OpenPopup("Entity");

    if (ImGui::BeginPopup("Entity"))
    {
        if (ImGui::Button("Copy Entity"))
        {
            currentCopyType = CopyType_Entity;
            copiedEntity = std::make_shared<Entity>(*selectedEntity);
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Delete Entity"))
        {
            entitiesListPregame.erase(std::remove(entitiesListPregame.begin(), entitiesListPregame.end(), *selectedEntity), entitiesListPregame.end());
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Locate Entity"))
        {
            LocateEntity(*selectedEntity);
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Rename Entity"))
        {
            shouldChangeObjectName = true;
            showManipulateEntityPopup = false;
        }
        else if (ImGui::Button("Duplicate Entity"))
        {
            DuplicateEntity(*selectedEntity);
            showManipulateEntityPopup = false;
        }
        else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            showManipulateEntityPopup = false;
        }

        ImGui::EndPopup();
    }
}

void DrawCameraTree(int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = 0;

    if (selectedGameObjectType == "camera") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    const char icon[] = ICON_FA_CAMERA;
    const char space[] = " ";
    std::string buttonName = std::string(icon) + space + "Camera";

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx(buttonName.c_str(), nodeFlags);
    ImGui::PopStyleColor();


    if (ImGui::IsItemClicked()) {
        active = index;
        selectedGameObjectType = "camera";
    }
    index++;
    if (isNodeOpen) ImGui::TreePop();
}

void DrawEntityTree(Entity& entity, int active, int& index, int depth) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;

    if (selectedEntity == &entity && selectedGameObjectType == "entity") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;

        if (shouldChangeObjectName) {
            char nameBuffer[256];

            size_t bufferSize = sizeof(nameBuffer);
            const char* source = entity.name.c_str();

            strncpy(nameBuffer, source, bufferSize - 1);
            nameBuffer[bufferSize - 1] = '\0';

            if (ImGui::InputText("##LightName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                entity.name = nameBuffer;
                shouldChangeObjectName = false;
            }
        }
    }

    const char icon[] = ICON_FA_CUBE;
    const char space[] = " ";
    std::string entityName = std::string(icon) + space + entity.name;

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx((void*)&entity, nodeFlags, entityName.c_str());
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))
    {
        showManipulateEntityPopup = true;
    }

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

        ImGui::SetDragDropPayload("CHILD_ENTITY_PAYLOAD", &entity.id, sizeof(int));
        ImGui::TreeNodeEx((void*)&entity, nodeFlags | ImGuiTreeNodeFlags_Selected, entity.name.c_str());
        ImGui::PopStyleColor();

        ImGui::EndDragDropSource();
    }

    // Drag and drop target
    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_LIGHT_PAYLOAD");
        if (payload) {
            if (payload->DataSize == sizeof(Light)) {
                Light& droppedLight = *(Light*)payload->Data;;
                entity.addChild(droppedLight);

                auto it = std::find(lights.begin(), lights.end(), droppedLight);
                if (it != lights.end()) {
                    lights.erase(it);
                } else {
                    std::cout << "Light not found." << std::endl;
                }
            } else {
                std::cerr << "Invalid payload size!" << std::endl;
            }
        }
        ImGui::EndDragDropTarget();
    }


    if (ImGui::BeginDragDropTarget()) {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CHILD_ENTITY_PAYLOAD");
        if (payload) {
            // Retrieve the entity ID from the payload data.
            int droppedEntityID = *(const int*)payload->Data;

            auto it = std::find_if(entitiesListPregame.begin(), entitiesListPregame.end(), [droppedEntityID](const Entity& entity) {
                return entity.id == droppedEntityID;
            });

            if (it != entitiesListPregame.end()) {
                Entity* foundEntity = (Entity*)&*it;
                foundEntity->isChild = true;
                entity.addChild(*foundEntity);

                auto it = std::find(entitiesListPregame.begin(), entitiesListPregame.end(), foundEntity);
                
                if (it != entitiesListPregame.end()) {
                    entitiesListPregame.erase(it);
                }
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::IsItemClicked() || showManipulateEntityPopup) {
        selectedEntity = &entity;
        active = index;
        selectedGameObjectType = "entity";
        objectInInspector = &entity;
    }


    if (isNodeOpen) {
        for (int childIndex = 0; childIndex < entity.children.size(); childIndex++) {
            index++;
            std::variant<Entity*, Light*, Text*, LitButton*> childVariant = entity.children[childIndex];

            std::visit([&](auto&& child) {
                using T = std::decay_t<decltype(child)>;
                if constexpr (std::is_same_v<T, Entity*>) {
                    DrawEntityTree(*child, active, index, depth + 1);
                } else if constexpr (std::is_same_v<T, Light*>) {
                    auto it = std::find_if(lights.begin(), lights.end(),
                        [&](const Light& light) { return light == *child; });

                    int distance = (it != lights.end()) ? std::distance(lights.begin(), it) : -1;

                    std::cout << "Distance: " << distance << std::endl << "Size: " << lights.size() << std::endl << std::endl;

                    DrawLightTree((Light&)child, lightsInfo[distance], active, index);
                } else if constexpr (std::is_same_v<T, Text*>) {
                    DrawTextElementsTree(*child, active, index);
                } else if constexpr (std::is_same_v<T, LitButton*>) {
                    DrawButtonTree(*child, active, index);
                }
            }, childVariant);
        }
        ImGui::TreePop();
    }

}



void DrawLightTree(Light& light, AdditionalLightInfo& light_info, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selectedLight == &light && selectedGameObjectType == "light") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (shouldChangeObjectName) {
            char nameBuffer[256];

            size_t bufferSize = sizeof(nameBuffer);
            const char* source = light_info.name.c_str();

            strncpy(nameBuffer, source, bufferSize - 1);
            nameBuffer[bufferSize - 1] = '\0';

            if (ImGui::InputText("##LightName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                light_info.name = nameBuffer;
                shouldChangeObjectName = false;
            }
        }
    }

    const char icon[] = ICON_FA_LIGHTBULB;
    const char space[] = " ";

    std::string lightName;
    try {
        lightName.reserve(std::string(icon).length() + std::string(space).length() + light_info.name.length() + 1);  // +1 for null terminator
        lightName = std::string(icon) + space + light_info.name;
    } catch (const std::bad_alloc& e) {
        lightName = std::string(icon) + space + "Error Getting Light Name";
    }

    bool isNodeOpen = false;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    isNodeOpen = ImGui::TreeNodeEx((void*)&light, nodeFlags, lightName.c_str());
    ImGui::PopStyleColor();

    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));

        ImGui::SetDragDropPayload("CHILD_LIGHT_PAYLOAD", &light, sizeof(Light));
        ImGui::TreeNodeEx((void*)&light, nodeFlags | ImGuiTreeNodeFlags_Selected, lightName.c_str());
        ImGui::PopStyleColor();

        ImGui::EndDragDropSource();
    }

    if (ImGui::IsItemClicked()) {
        selectedLight = &light;
        active = index;
        selectedGameObjectType = "light";
        objectInInspector = &light;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}


void DrawTextElementsTree(Text& text, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selectedTextElement == &text && selectedGameObjectType == "text") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (shouldChangeObjectName) {
            char nameBuffer[256];
            size_t bufferSize = sizeof(nameBuffer);
            const char* source = text.name.c_str();

            strncpy(nameBuffer, source, bufferSize - 1);
            nameBuffer[bufferSize - 1] = '\0';

            if (ImGui::InputText("##TextName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                text.name = nameBuffer;
                shouldChangeObjectName = false;
            }
        }
    }
    const char icon[] = ICON_FA_TEXT_SLASH;
    const char space[] = " ";
    
    std::string textName = std::string(icon) + space + text.name;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&text, nodeFlags, textName.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selectedTextElement = &text;
        active = index;
        selectedGameObjectType = "text";
        objectInInspector = &text;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}


void DrawButtonTree(LitButton& button, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selectedButton == &button && selectedGameObjectType == "button") {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
        if (shouldChangeObjectName) {
            char nameBuffer[256];
            size_t bufferSize = sizeof(nameBuffer);
            const char* source = button.name.c_str();

            strncpy(nameBuffer, source, bufferSize - 1);
            nameBuffer[bufferSize - 1] = '\0';

            if (ImGui::InputText("##ButtonName", nameBuffer, sizeof(nameBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
                button.name = nameBuffer;
                shouldChangeObjectName = false;
            }
        }
    }
    const char icon[] = ICON_FA_STOP;
    const char space[] = " ";
    
    std::string buttonName = std::string(icon) + space + button.name;

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&button, nodeFlags, buttonName.c_str());
    ImGui::PopStyleColor();
    if (ImGui::IsItemClicked()) {
        selectedButton = &button;
        active = index;
        selectedGameObjectType = "button";
        objectInInspector = &button;
    }

    if (isNodeOpen) {
        ImGui::TreePop();
    }
}



void ImGuiListViewEx(vector<string>& items, int& focus, int& scroll, int& active) {
    ImVec2 screen = ImGui::GetIO().DisplaySize;
    
    ImVec2 childSize = ImVec2(
        ImGui::GetWindowSize().x - 30,
        ImGui::GetWindowSize().y - 150);

    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

    if ((ImGui::IsWindowFocused() || ImGui::IsWindowHovered() || ImGui::IsItemHovered()) && IsKeyDown(KEY_F2))
        shouldChangeObjectName = true;

    if (ImGui::IsWindowFocused() && IsKeyDown(KEY_ESCAPE))
        shouldChangeObjectName = false;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10,10));

    int currentAmount = 0;
    int index = 0;

    ImGui::PopFont();
    ImGui::PushFont(s_Fonts["ImGui Default"]);

    DrawCameraTree(active, index);

    for (Entity& entity : entitiesListPregame) {
        DrawEntityTree(entity, active, index);
    }

    int lightsIndex = 0;
    for (Light& light : lights) {
        if (light.isChild) continue;
        DrawLightTree(light, lightsInfo[lightsIndex], active, index);
        lightsIndex++;
    }

    for (Text& text : textElements) {
        DrawTextElementsTree(text, active, index);
    }

    for (LitButton& button : litButtons) {
        DrawButtonTree(button, active, index);
    }

    ImGui::PopFont();
    ImGui::PushFont(s_Fonts["Default"]);

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);

    ImGui::EndChild();

    ManipulateEntityPopup();
}


void EntitiesList()
{

    ImGui::PushFont(s_Fonts["Default"]);
    ImGui::Begin((std::string(ICON_FA_BARS) + " Objects List").c_str(), NULL);
    ImGui::PopFont();

    updateListViewExList(entitiesListPregame, lightsListPregame);
    ImGuiListViewEx(objectNames, listViewExFocus, listViewExScrollIndex, listViewExActive);

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.3f, 0.3f, 0.3f, 0.2f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.25f, 0.3f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

    ImVec2 buttonSize = ImVec2(50, 50);

    if (ImGui::ImageButton((ImTextureID)&runTexture, buttonSize)) {
        entitiesList.assign(entitiesListPregame.begin(), entitiesListPregame.end());
        
        // DisableCursor();
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
        
        for (Entity& entity : entitiesListPregame)
        {
            entity.resetPhysics();
        }

        for (Entity& entity : entitiesList)
        {
            entity.resetPhysics();
        }
    }


    if (IsKeyPressed(KEY_F1))
    {
        openAboutPage();
    }

    if (IsKeyPressed(KEY_F2) && !inGamePreview && !ImGui::IsWindowFocused)
    {
        openManualPage();
    }


    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);

    ImGui::End();
}