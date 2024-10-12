#ifndef TOOLTIP_H
#define TOOLTIP_H

class Tooltip {
public:
    std::string text;
    Vector2 position;
    Vector2 size;
    Color backgroundColor = DARKGRAY;
    Color textColor = WHITE;
    Font font = GetFontDefault();

    void Draw();
};

#endif // TOOLTIP_H