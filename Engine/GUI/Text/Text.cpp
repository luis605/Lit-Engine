#include "../../../include_all.h"
#include "Text.h"

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


void DrawTextElement(Text element)
{
    int lineHeight = element.fontSize + element.spacing;
    Vector3 currentPosition = element.position;

    std::istringstream ss(element.text);
    std::string line;
    int textWidth = 0; 
    int numLines = 0;  

    while (std::getline(ss, line, '\n'))
    {
        int lineWidth = MeasureText(line.c_str(), element.fontSize);
        textWidth = std::max(textWidth, lineWidth);
        numLines++;
    }
    
    if (element.backgroundColor.a > 0)
    {        
        int textHeight = numLines * (lineHeight + element.spacing);
        int bgWidth = textWidth + 2 * element.backgroundRoundiness + element.padding;
        int bgHeight = textHeight + 2 * element.backgroundRoundiness + element.padding;

        Vector3 bgPosition = { currentPosition.x - element.backgroundRoundiness - element.padding / 2, currentPosition.y - element.backgroundRoundiness - element.padding / 2, currentPosition.z };
        
        DrawRectangleRounded({bgPosition.x, bgPosition.y, bgWidth, bgHeight}, element.backgroundRoundiness/10, 30, element.backgroundColor);
    }

    ss.clear();
    ss.seekg(0);
    
    while (std::getline(ss, line, '\n'))
    {
        DrawText(line.c_str(), (int)currentPosition.x, (int)currentPosition.y, element.fontSize, element.color);
        currentPosition.y += lineHeight;
    }
}

void DrawTextElements()
{

    std::sort(textElements.begin(), textElements.end(), [](const Text& a, const Text& b)
    {
        return a.position.z < b.position.z;
    });

    for (const auto& element : textElements)
    {
        DrawTextElement(element);
    }
}

