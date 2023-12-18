#include "../../../include_all.h"
#include "Text.h"

bool Text::IsPressed() {
    // Assuming you have a way to get mouse input, you can check if the mouse
    // is within the bounds of the text element.
    Vector2 mousePos = GetMousePosition();
    return CheckCollisionPointRec(mousePos, bounds) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
}

void Text::Draw() {
    int lineHeight = fontSize + spacing;
    Vector3 currentPosition = position;

    std::istringstream ss(text);
    std::string line;
    int textWidth = 0;
    int numLines = 0;

    while (std::getline(ss, line, '\n')) {
        int lineWidth = MeasureText(line.c_str(), fontSize);
        textWidth = std::max(textWidth, lineWidth);
        numLines++;
    }

    bounds = {
        currentPosition.x - backgroundRoundness - padding / 2,
        currentPosition.y - backgroundRoundness - padding / 2,
        static_cast<float>(textWidth + 2 * backgroundRoundness + padding),
        static_cast<float>(numLines * (lineHeight + spacing) + 2 * backgroundRoundness + padding)
    };

    if (backgroundColor.a > 0) {
        DrawRectangleRounded(bounds, backgroundRoundness / 10, 30, backgroundColor);
    }

    ss.clear();
    ss.seekg(0);

    while (std::getline(ss, line, '\n')) {
        DrawText(line.c_str(), static_cast<int>(currentPosition.x), static_cast<int>(currentPosition.y), fontSize, color);
        currentPosition.y += lineHeight;
    }

#ifndef GAME_SHIPPING
    if (IsPressed())
    {
        selected_textElement = this;
        selected_game_object_type = "text";
    }

    if (selected_textElement == this && selected_game_object_type == "text")
        selected = true;
    else
        selected = false;

    if (selectable && selected)
    {
        DrawRectangleLines(bounds.x, bounds.y, bounds.width, bounds.height, BLUE);
    }
#endif
}

void DrawTextElements() {
    std::sort(textElements.begin(), textElements.end(), [](const Text& a, const Text& b) {
        return a.position.z < b.position.z;
    });

    for (Text& element : textElements) {
        element.Draw();
    }
}



Text& AddText(const char* text, Vector3 position, int fontSize, Color color)
{
    textElements.emplace_back();
    Text& element = textElements.back();
    element.text = text;
    element.position = position;
    element.fontSize = fontSize;
    element.color = color;
    return element;
}
