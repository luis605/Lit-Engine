#ifndef TEXT_H
#define TEXT_H

#include "../../../include_all.h"

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

std::vector<Text> textElements;

#endif // TEXT_H
