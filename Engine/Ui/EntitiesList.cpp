#include "../../include_all.h"
#include "../../globals.h"



void updateListViewExList(vector<Entity>& entities, vector<Light>& lights) {
    listViewExList.clear();
    objectNames.clear();

    for (int index = 0; index < entities.size() + lights.size(); index++) {
        string name;
        if (index < entities.size()) {
            name = entities[index].name;
            listViewExListTypes.push_back("entity");
        } else {
            name = lights[index - entities.size()].name;
            listViewExListTypes.push_back("light");
        }
        objectNames.push_back(name);
    }

    
    // Resize listViewExList to match the size of objectNames
    listViewExList.reserve(100000000);
    listViewExList.resize(listViewExList.size()+1);

    // Set the values of listViewExList to the character pointers to the names in objectNames
    for (int i = 0; i < objectNames.size(); i++) {
        listViewExList[i] = objectNames[i].c_str();
    }
}



bool operator==(const Entity& e, const Entity* ptr) {
    return &e == ptr;
}


void AddEntity()
{
    const int POPUP_WIDTH = 600;
    const int POPUP_HEIGHT = 650;

    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create)
    {
        Color entity_color_raylib = {
            static_cast<unsigned char>(color.x * 255),
            static_cast<unsigned char>(color.y * 255),
            static_cast<unsigned char>(color.z * 255),
            static_cast<unsigned char>(color.w * 255)
        };

        Entity entity_create;
        entity_create.setColor(entity_color_raylib);
        entity_create.setScale(Vector3{scale, scale, scale});
        entity_create.setName(name);
        entity_create.isChild = is_create_entity_a_child;
        entity_create.setModel("assets/models/tree.obj");
        entity_create.setShader(shader);

        if (!entities_list_pregame.empty())
        {
            string id = to_string(stoi(entities_list_pregame.back().id) + 1);
            entity_create.id = id;
        }
        else
            entity_create.id = "0";

        if (!is_create_entity_a_child)
            entities_list_pregame.push_back(entity_create);
        else
        {
            std::cout << "Adding child entities..." << std::endl;
            entities_list_pregame.back().addChild(entity_create);
        }

        int last_entity_index = entities_list_pregame.size() - 1;
        listViewExActive = last_entity_index;

        create = false;
        canAddEntity = false;
    }
    else if (canAddEntity)
    {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.6f, 0.6f, 0.6f, 0.6f)); // light gray

        ImGui::Begin("Entities");

        ImVec2 size = ImGui::GetContentRegionAvail();

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 50.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2 - 250);
        ImGui::SetCursorPosY(size.y / 4);
        ImGui::Button("Entity Add Menu", ImVec2(500,100));

        ImGui::PopStyleColor(4);

        /* Scale Slider */
        ImGui::Text("Scale: ");
        ImGui::SameLine();
        ImGui::SliderFloat(" ", &scale, 0.1f, 100.0f);

        /* Name Input */
        ImGui::InputText("##text_input_box", name, sizeof(name));
        
        /* Color Picker */
        ImGui::Text("Color: ");
        ImGui::SameLine();
        ImGui::ColorEdit4(" ", (float*)&color, ImGuiColorEditFlags_NoInputs);

        /* Is Children */
        ImGui::Checkbox("Is Children: ", &is_create_entity_a_child);

        /* Create BTN */
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.14, 0.37, 0.15, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18, 0.48, 0.19, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));

        ImGui::SetCursorPosX(size.x / 2);
        ImGui::SetCursorPosY(size.y / 1.1);
        bool create_entity_btn = ImGui::Button("Create", ImVec2(200,50));
        if (create_entity_btn)
        {
            canAddEntity = false;
            create = true;
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::End();
    }
}












void DrawEntityTree(Entity& entity, int active, int& index) {
    ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
    if (selected_entity == &entity) {
        nodeFlags |= ImGuiTreeNodeFlags_Selected;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));  // Custom text color
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 5.0f);  // Rounded corners for the frame
    bool isNodeOpen = ImGui::TreeNodeEx((void*)&entity, nodeFlags, entity.name.c_str());
    ImGui::PopStyleVar();  // Restore the default frame rounding
    ImGui::PopStyleColor();  // Restore the default text color
    if (ImGui::IsItemClicked()) {
        selected_entity = &entity;
        active = index;
        selected_gameObject_type = "entity";
        object_in_inspector = &entity;
        std::cout << "index: " << index << std::endl;
    }

    if (isNodeOpen) {
        for (int childIndex = 0; childIndex < entity.children.size(); childIndex++) {
            index++;
            Entity* child = entity.children[childIndex];
            DrawEntityTree(*child, active, index);
        }
        ImGui::TreePop();
    }
}



int AmountOfEntities(const std::vector<Entity>& entities, int current_amount)
{
    for (const Entity& entity : entities)
    {
        current_amount++;
        if (!entity.children.empty())
        {
            for (int index = 0; index < entity.children.size(); index++)
                current_amount = AmountOfEntities({*entity.children[index]}, current_amount);
        }
    }
    return current_amount;
}






int ImGuiListViewEx(vector<string>& items, int& focus, int& scroll, int& active) {
    ImGui::SetNextWindowSize(ImVec2(400, 200)); // set the container size

    ImVec2 screen = ImGui::GetIO().DisplaySize;
    ImVec2 childSize = ImVec2(.2 * screen.x, .7 * screen.y);
    ImGui::BeginChild("Entities List", childSize, true, ImGuiWindowFlags_HorizontalScrollbar);

    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray

    ImGui::PushItemWidth(-1);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0,10));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(10,10));

    int currentAmount = 0;
    int amountOfEntities = AmountOfEntities(entities_list_pregame, currentAmount);
    int index = 0;

    for (Entity& entity : entities_list_pregame) {
        DrawEntityTree(entity, active, index);
    }

    


    

    ImGui::PopStyleVar(3);
    ImGui::PopItemWidth();

    ImGui::PopStyleColor(3);



    ImGui::EndChild();
    return active;


}




// Widgets
void EntitiesList()
{

    ImGui::Text("Mouse Position: (%g, %g)", ImGui::GetMousePos().x, ImGui::GetMousePos().y);

/* 
    struct sigaction sa;

    memset(&sa, 0, sizeof(struct sigaction));
    sigemptyset(&sa.sa_mask);
    sa.sa_sigaction = segfault_sigaction;
    sa.sa_flags   = SA_SIGINFO;

    sigaction(SIGSEGV, &sa, NULL);
 */



    updateListViewExList(entities_list_pregame, lights_list_pregame);

    // Translate the rectangles coordinates to ImGui coordinates
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    Rectangle rec = { pos.x, pos.y, size.x, size.y };

    listViewExActive = ImGuiListViewEx(objectNames, listViewExFocus, listViewExScrollIndex, listViewExActive);


    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 0.9f)); // light gray
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f)); // black
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.5f, 0.5f, 0.5f, 0.5f)); // light gray


    bool add_entity = ImGui::Button("Add Entity", ImVec2(120,40));
    if (add_entity)
    {
        sprintf(name, "Entity Name");
        canAddEntity = true;
    }

    ImGui::SameLine();


    bool add_light = ImGui::Button("Add Light", ImVec2(120,40));
    if (add_light)
    {
        canAddLight = true;
        NewLight({4, 4, 4}, WHITE);
        UpdateLightsBuffer();
    }


    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();


    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0,0));
    if (ImGui::ImageButton((ImTextureID)&run_texture, ImVec2(60, 60)))
    {
        cout << "Running Game" << endl;
        entities_list.assign(entities_list_pregame.begin(), entities_list_pregame.end()); // Copy the engine's list of entities to the ingame entities list
        InitGameCamera();
        in_game_preview = true;
    }

    ImGui::SameLine();
    
    if (ImGui::ImageButton((ImTextureID)&pause_texture, ImVec2(60, 60)))
    {
        cout << "Stopping Game" << endl;
        in_game_preview = false;

    }

    
    ImGui::PopStyleVar();

    if (!entities_list_pregame.empty() || !lights_list_pregame.empty()) {
        // if (selected_gameObject_type == "entity")
        //     object_in_inspector = &entities_list_pregame[listViewExActive];
        // else
        //     object_in_inspector = &lights_list_pregame[listViewExActive];
    } else {
        static Entity default_entity;
        object_in_inspector = &default_entity;
    }

}

