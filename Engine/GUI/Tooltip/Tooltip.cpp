class Tooltip {
public:
    std::string text;
    Vector2 position;
    Vector2 size;
    Color backgroundColor = DARKGRAY;
    Color textColor = WHITE;
    Font font = GetFontDefault();

    void Draw() {
        DrawRectangleRounded({ position.x, position.y, size.x, size.y }, 0.5f, 10, backgroundColor);
        DrawText(text.c_str(), static_cast<int>(position.x) + 5, static_cast<int>(position.y) + 5, font.baseSize, textColor);
    }
};
