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


void AddEntity(void)
{
    // Define the layout of the popup
    const int POPUP_WIDTH = 600;
    const int POPUP_HEIGHT = 650;

    // Calculate the position of the popup
    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create) {
        // Conversion imgui color to raylib color
        Color entity_color_raylib = (Color){ (unsigned char)(color.x*255), (unsigned char)(color.y*255), (unsigned char)(color.z*255), (unsigned char)(color.w*255) };

        // Create Entity
        Entity entity_create = Entity();
        entity_create.setColor(entity_color_raylib);
        entity_create.setScale(Vector3 { scale, scale, scale, });
        entity_create.setName(name);
        entity_create.isChildren = IsChildren;
        entity_create.setModel("assets/models/tree.obj");

        if (!IsChildren)
            entities_list_pregame.push_back(entity_create);
        else
        {
            std::cout << "Adding child entities..." << std::endl;
            entities_list_pregame.back().addChild(entity_create);
        }
        // else if (holds_alternative<Entity*>(object_in_inspector))
        // {
        //     Entity *entity_in_inspector = get<Entity*>(object_in_inspector); 
        //     entity_in_inspector->addChild(entities_list_pregame.back());
        // }
        
        int last_index = entities_list_pregame.size() - 1;
        listViewExActive = last_index;

        old_child_selected = IsChildren;
        old_parent_selected = !IsChildren;

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
        ImGui::Checkbox("Is Children: ", &IsChildren);

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



void AddLight()
{
    if (canAddLight)
    {
        cout << "AddLight" << endl;
        Light light_create = CreateLight(LIGHT_POINT, (Vector3){ -2, 1, -2 }, Vector3Zero(), RED, shader);
        lights_list_pregame.push_back(light_create);
        canAddLight = false;
    }
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



    // Buttons
    for (int i = 0; i < items.size(); i++)
    {
        int old_active = active;
        int children_index = i;

        bool button_clicked = false;

        float parent_button_x = 20;



        /*
        Mixed lights and entities names (items), so if items.size() is greater than entities_list_pregame.size(), when we do entities_list_pregame[i] will get segfault (because it will be out of bounds).
        Easy fix: divide entities_list_pregame and lighs_list_pregame in 2 (if-else) conditions and draw the buttons seperately  
        */
       
        if (i < entities_list_pregame.size())
        {
            if (!entities_list_pregame[i].isChildren)
            {
                if ((i == old_active) && old_parent_selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.9f)); // dark gray

                ImGui::Indent(10);

                    string ListViewExButtonName = ICON_FA_CUBE " " + items[i] + "##" + to_string(i);

                    if (ImGui::Button(ListViewExButtonName.c_str(), ImVec2(120,40))) {
                        parent_selected = true;
                        child_selected  = false;
                        button_clicked  = true;
                        focus           = i;
                        active          = i;
                        selected_gameObject_type = "entity";
                    }

                ImGui::Unindent(10);

                if ((i == old_active) && old_parent_selected) ImGui::PopStyleColor();
                button_clicked      = false;
                old_parent_selected = parent_selected;
                old_child_selected  = child_selected;

                old_active = active;
            }
        }
        else
        {
            if (!lights_list_pregame[i].isChildren)
            {
                if ((i == old_active) && old_parent_selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.9f)); // dark gray

                ImGui::Indent(10);

                    string ListViewExButtonName = ICON_FA_LIGHTBULB " " + items[i] + "##" + to_string(i);

                    if (ImGui::Button(ListViewExButtonName.c_str(), ImVec2(120,40))) {
                        parent_selected = true;
                        child_selected  = false;
                        button_clicked  = true;
                        focus           = i;
                        active          = i;
                        selected_gameObject_type = "light";
                    }

                ImGui::Unindent(10);

                if ((i == old_active) && old_parent_selected) ImGui::PopStyleColor();
                button_clicked      = false;
                old_parent_selected = parent_selected;
                old_child_selected  = child_selected;

                old_active = active;
            }
        }




        if (entities_list_pregame.size() <= i) break;

        if (!entities_list_pregame[i].children.empty())
        {
            for (int index = 0; index < entities_list_pregame[i].children.size(); index++)
            {
                children_index = index + 1;

                if (children_index < 0 || children_index >= entities_list_pregame.size()) {
                    children_index--;
                    // cout << "Invalid children index." << endl;
                    if (children_index < 0 || children_index >= entities_list_pregame.size()) continue;
                }


                if ((children_index == old_active) && old_child_selected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 0.9f)); // dark gray
                
                ImDrawList* draw_list = ImGui::GetWindowDrawList();

                ImGui::Spacing();
                ImGui::Indent(30);


                string ChildButtonName;
                ChildButtonName.append(ICON_FA_CUBE " ");
                if (entities_list_pregame[children_index].name.size() > 10000)
                {
                    ChildButtonName += "GOT ERROR";
                } else 
                {
                    ChildButtonName += entities_list_pregame[children_index].name;
                }
                ChildButtonName.append("##Children_");
                ChildButtonName.append(to_string(i));
                const char* result = ChildButtonName.c_str();


                if (ImGui::Button(result, ImVec2(120,40)))
                {
                    child_selected  = true;
                    parent_selected = false;
                    button_clicked  = true;
                    focus           = children_index;
                    active          = children_index;
                }

                if (entities_list_pregame[children_index].isParent)
                {
                    parent_button_x = ImGui::GetCursorPos().x;
                }


                ImGui::Unindent(30);
                

                /* Vertical Line */
                float last_button_height = ImGui::GetItemRectSize().y;

                ImVec2 point_1 = ImVec2(parent_button_x, 10);
                ImVec2 point_2;
                
                point_2.x = point_1.x;
                point_2.y = ImGui::GetCursorScreenPos().y - last_button_height/2;
                
                /* Horizontal Line */
                ImVec2 point_3 = ImVec2(ImGui::GetCursorScreenPos().x, point_2.y);
                ImVec2 point_4;
                
                point_4.x = ImGui::GetItemRectMax().x - ImGui::GetItemRectSize().x;
                point_4.y = ImGui::GetCursorScreenPos().y - last_button_height/2;

                /* Draw the lines */
                draw_list->AddLine(point_1, point_2, ImColor(255, 255, 0), 2.0f);
                draw_list->AddLine(point_3, point_4, ImColor(255, 255, 0), 2.0f);

                if ((children_index == old_active) && old_child_selected) ImGui::PopStyleColor();
                button_clicked      = false;
                old_parent_selected = parent_selected;
                old_child_selected  = child_selected;

                old_active = children_index;
            }

        }




        


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
        if (selected_gameObject_type == "entity")
            object_in_inspector = &entities_list_pregame[listViewExActive];
        else
            object_in_inspector = &lights_list_pregame[listViewExActive];
    } else {
        static Entity default_entity;
        object_in_inspector = &default_entity;
    }

}

