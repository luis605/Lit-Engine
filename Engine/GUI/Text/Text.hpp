/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef TEXT_H
#define TEXT_H

#include <Engine/Scripting/math.hpp>
#include <string>
#include <vector>

class Text {
public:
    std::string text;
    std::string name = "Text";
    LitVector3 position;
    float fontSize;
    float spacing = 4;
    Color color;
    Color backgroundColor = { 0, 0, 0, 0 };
    float backgroundRoundness = 0;
    float padding = 25;
    Rectangle bounds;
    bool selectable = true;
    bool selected = false;

public:
    Text() : text(""), position(0, 0, 0), fontSize(12), color(RAYWHITE) {
    }

    Text(const char* text, Vector3 position, int fontSize, Color color)
        : text(text), position(position), fontSize(fontSize), color(color) {
    }

    bool IsPressed();
    void Draw();
};

extern std::vector<Text> textElements;

void DrawTextElements();
Text& AddText(const char* text, Vector3 position, int fontSize, Color color);

#endif // TEXT_H
