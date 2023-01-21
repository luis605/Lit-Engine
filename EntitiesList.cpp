
/* Entities List */

// Variables
int listViewExScrollIndex = 0;
int listViewExActive = 0;
int listViewExFocus = 0;
std::vector<char*> listViewExList;
bool canAddEntity = false;


std::vector<std::string> entityNames;


char name[256] = { 0 };
int scale = 1;
Color color = WHITE;
bool showNextTime = true;
bool create = false;


void updateListViewExList(vector<Entity>& entities) {
    // Clear the current values of listViewExList
    listViewExList.clear();

    // Clear the current values of entityNames
    entityNames.clear();

    // Store the names of the entities in entityNames
    for (int i = 0; i < entities.size(); i++) {
        entityNames.push_back(entities[i].getName());
    }

    
    // Resize listViewExList to match the size of entityNames
    listViewExList.reserve(100000000);
    listViewExList.resize(listViewExList.size()+1);

    // Set the values of listViewExList to the character pointers to the names in entityNames
    for (int i = 0; i < entityNames.size(); i++) {
        listViewExList[i] = entityNames[i].c_str();
    }
}




void AddEntity(void)
{

    // Define the layout of the popup
    const int POPUP_WIDTH = 600;
    const int POPUP_HEIGHT = 650;
    const int INPUT_FIELD_WIDTH = 360;
    const int INPUT_FIELD_HEIGHT = 20;

    // Calculate the position of the popup
    int popupX = GetScreenWidth() / 4.5;
    int popupY = (GetScreenHeight() - POPUP_HEIGHT) / 6;

    if (create) {
        // Add the entity to the list
        Entity entity_create;
        entity_create.setColor(color);
        entity_create.setScale(Vector3 { scale, scale, scale, });
        entity_create.setName("New Entity");
        entity_create.setModel("assets/models/tree.obj");
        entities_list.push_back(entity_create);
        updateListViewExList(entities_list);
        create = false;
        canAddEntity = false;

        std::cout << "\n-----------------------\nEntities:\n---------\n";
        for (int i = 0; i < entityNames.size(); i++) {
            std::cout << listViewExList[i] << std::endl;
        }

        
    }
    else
    {

        // Popup Background
        Rectangle rec = { popupX, popupY, POPUP_WIDTH, POPUP_HEIGHT };
        DrawRectanglePro(rec, (Vector2){0,0}, 0.0f, WHITE);

        // Title

        // Set the rectangle bounds and rounding value
        Rectangle title_rec = { screenWidth/4, screenHeight/9, 500, 100 };
        float roundness = 20.0f;
        int segments = 10;

        // Draw Title
        DrawRectangleRounded(title_rec, roundness, segments, DARKGRAY);
        DrawText(TextFormat("Add Entity Menu"), title_rec.x+140, title_rec.y+40, 20, BLACK);

        // Scale
        scale = GuiSliderBar((Rectangle){ popupX + 140, popupY + 250, 200, 20 }, "Scale: ", TextFormat("%i", (int)scale), scale, 0.1, 100);
        
        // Color
        color = GuiColorPicker((Rectangle){ popupX + 100, popupY + 300, 360, 260 }, "Color: ", color);

        // Show Next Time
        // showNextTime = GuiCheckBox(Rectangle{ popupX + 120, popupY + 600, 20, 20 }, "Show Next Time", showNextTime); 

        // Create Entity Button
        create = Button(popupX + 400, popupY + 600, 100, 30, "Create Entity");

    }
}


Entity& EntitiesList()
{
    updateListViewExList(entities_list);
    bool add_entity = Button(screenWidth*.1, screenHeight*.1, 120, 30, "Add Entity");
    if (add_entity) 
    {
        canAddEntity = true;
    }
 //GuiListViewEx((Rectangle){ 165, 180, 140, 700 }, listViewExList, 8, &listViewExFocus, &listViewExScrollIndex, listViewExActive);
 
//     for (int i = 0; i < entityNames.size(); i++) {
//         entities_list[i].setColor(BLUE);
// //        std::cout << listViewExActive << std::endl;
//     }
    listViewExActive = GuiListViewEx((Rectangle){ 165, 180, 140, 700 }, listViewExList.data(), entityNames.size(), &listViewExFocus, &listViewExScrollIndex, listViewExActive);

    return entities_list[listViewExActive];
}
