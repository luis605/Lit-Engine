#include "globals.h"




bool nameBufferInitialized = false;


void Inspector()
{
    // Get Entity
    Entity &entity_in_inspector = EntitiesList();

    // Update Values
    if (!nameBufferInitialized || entity_name != entity_in_inspector.name)
    {
        nameBuffer = entity_in_inspector.name;
        nameBufferInitialized = true;
    }



    // Define the dimensions and position of the inspector
    Rectangle inspectorRect = { screenWidth/1.5, screenHeight/5, 400, 300 };


    // Draw the title bar with a rounded rectangle and centered text
    std::stringstream title;
    title << "Inspecting '" << entity_in_inspector.name << "'";
    GuiLabel(Rectangle{ inspectorRect.x, inspectorRect.y, inspectorRect.width, 50 }, title.str().c_str());



    // Draw the main content area with a rectangle
    GuiPanel(Rectangle{ inspectorRect.x, inspectorRect.y + 50, inspectorRect.width, inspectorRect.height - 50 }, "");

    // Set the color of the button background
    GuiSetStyle(BUTTON, BASE, ColorToInt( RED ));


    if (GuiButton(Rectangle{ inspectorRect.x + 100, inspectorRect.y + 50, 100, 30 }, "DELETE"))
    {
        // delete *entity_in_inspector;
        // entityList.erase(it);
    }

    GuiSetStyle(BUTTON, BASE, ColorToInt(WHITE));


    // Add other GUI elements to the inspector as needed
    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 70, 100, 30 }, "Model Path:");
    if (GuiTextBox(Rectangle{ inspectorRect.x + 110, inspectorRect.y + 70, 100, 30 }, modelPathBuffer, 512, modelPathBufferEditMode)) modelPathBufferEditMode = !modelPathBufferEditMode;

    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 110, 100, 30 }, "Scale:");
    if (GuiTextBox(Rectangle{ inspectorRect.x + 110, inspectorRect.y + 110, 50, 30 }, scaleBufferX, 128, scaleBufferXEditMode)) scaleBufferXEditMode = !scaleBufferXEditMode;
    if (GuiTextBox(Rectangle{ inspectorRect.x + 170, inspectorRect.y + 110, 50, 30 }, scaleBufferY, 128, scaleBufferYEditMode)) scaleBufferYEditMode = !scaleBufferYEditMode;
    if (GuiTextBox(Rectangle{ inspectorRect.x + 230, inspectorRect.y + 110, 50, 30 }, scaleBufferZ, 128, scaleBufferZEditMode)) scaleBufferZEditMode = !scaleBufferZEditMode;

    // Update scale
    sscanf(scaleBufferX, "%f", &entity_scale.x);
    sscanf(scaleBufferY, "%f", &entity_scale.y);
    sscanf(scaleBufferZ, "%f", &entity_scale.z);
    entity_in_inspector.scale = entity_scale;

    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 150, 100, 30 }, "Position:");
    if (GuiTextBox(Rectangle{ inspectorRect.x + 110, inspectorRect.y + 150, 50, 30 }, positionBufferX, 128, positionBufferXEditMode)) positionBufferXEditMode = !positionBufferXEditMode;
    if (GuiTextBox(Rectangle{ inspectorRect.x + 170, inspectorRect.y + 150, 50, 30 }, positionBufferY, 128, positionBufferYEditMode)) positionBufferYEditMode = !positionBufferYEditMode;
    if (GuiTextBox(Rectangle{ inspectorRect.x + 230, inspectorRect.y + 150, 50, 30 }, positionBufferZ, 128, positionBufferZEditMode)) positionBufferZEditMode = !positionBufferZEditMode;

    // Update the position values from the text box
    sscanf(positionBufferX, "%f", &entity_position.x);
    sscanf(positionBufferY, "%f", &entity_position.y);
    sscanf(positionBufferZ, "%f", &entity_position.z);
    entity_in_inspector.position = entity_position;



    // Color
    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 190, 100, 30 }, "Color:");

    // Set the color of the button background
    GuiSetStyle(BUTTON, BASE, ColorToInt(entityColor));

    if (GuiButton(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 190, 200, 30 }, "Color"))
    {
        changeEntityColorPopup = true;
    }
    if (changeEntityColorPopup)
    {
        entityColor = GuiColorPicker(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 390, 300, 300}, "", entityColor);
        GuiSetStyle(BUTTON, BASE, ColorToInt(WHITE));
        setEntityColor = GuiButton(Rectangle{ inspectorRect.x + 340, inspectorRect.y + 390, 60, 300 }, "Set Color");
    }
    if (setEntityColor) // Update Color
    {
        changeEntityColorPopup = false;
        setEntityColor = false;
        entity_in_inspector.color = entityColor;
    }



    // Rotation
    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 230, 100, 30 }, "Rotation:");
    if (GuiTextBox(Rectangle{ inspectorRect.x + 110, inspectorRect.y + 230, 100, 30 }, rotationBuffer, 128, rotationBufferEditMode)) rotationBufferEditMode = !rotationBufferEditMode;

    // Update the rotation values from the text box
    sscanf(rotationBuffer, "%f,%f,%f", &entity_rotation.x, &entity_rotation.y, &entity_rotation.z);
    entity_in_inspector.rotation = entity_rotation;


    // Name
    GuiLabel(Rectangle{ inspectorRect.x + 10, inspectorRect.y + 270, 100, 30 }, "Name:");
    if (GuiTextBox(Rectangle{ inspectorRect.x + 110, inspectorRect.y + 270, 100, 30 }, nameBuffer.c_str(), 128, nameBufferEditMode)) nameBufferEditMode = !nameBufferEditMode;

    entity_in_inspector.name.resize(128);
    nameBuffer.resize(128);

    // Update name
    sscanf(nameBuffer.c_str(), "%s", &entity_name);
    entity_in_inspector.name = nameBuffer;

}