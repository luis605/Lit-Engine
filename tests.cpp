#include "raylib.h"

#include "rlgl.h"
#include "raymath.h"
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <sstream>


// Text
struct Text
{
    std::string text;
    Vector3 position;
    int fontSize                = 20;
    float spacing               = 4;
    Color color;
    Color backgroundColor       = {0,0,0,0};
    float backgroundRoundiness  = 0;
    float padding               = 25;
};

std::vector<Text> textElements;

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
    int textWidth = 0; // To calculate the width of the text
    int numLines = 0;  // To count the number of lines

    // Get the maximum width of all lines in the text
    while (std::getline(ss, line, '\n'))
    {
        int lineWidth = MeasureText(line.c_str(), element.fontSize);
        textWidth = std::max(textWidth, lineWidth);
        numLines++;
    }

    // Draw the background if the color alpha is greater than 0
    if (element.backgroundColor.a > 0)
    {
        // Calculate the size of the background rectangle to cover the whole text
        int textHeight = numLines * (lineHeight + element.spacing);
        int bgWidth = textWidth + 2 * element.backgroundRoundiness + element.padding;
        int bgHeight = textHeight + 2 * element.backgroundRoundiness + element.padding;

        // Calculate the position of the background rectangle
        Vector3 bgPosition = { currentPosition.x - element.backgroundRoundiness - element.padding / 2, currentPosition.y - element.backgroundRoundiness - element.padding / 2, currentPosition.z };
        // Draw the rounded rectangle
        DrawRectangleRounded({bgPosition.x, bgPosition.y, bgWidth, bgHeight}, element.backgroundRoundiness/10, 30, element.backgroundColor);
    }

    // Reset the stream for text rendering
    ss.clear();
    ss.seekg(0);

    // Draw each line of the text
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



// Lerp Colors

Color LerpColor(Color startColor, Color endColor, float t)
{
    unsigned char r = static_cast<unsigned char>(startColor.r + (endColor.r - startColor.r) * t);
    unsigned char g = static_cast<unsigned char>(startColor.g + (endColor.g - startColor.g) * t);
    unsigned char b = static_cast<unsigned char>(startColor.b + (endColor.b - startColor.b) * t);
    unsigned char a = static_cast<unsigned char>(startColor.a + (endColor.a - startColor.a) * t);

    return { r, g, b, a };
}



// Tooltip

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



// Buttons
class LitButton {
public:
    Vector3 position;
    Vector2 size;
    Color color = LIGHTGRAY;
    Color pressedColor = DARKGRAY;
    Color hoverColor = GRAY;
    Color disabledButton = GRAY;
    Color disabledText = DARKGRAY;
    Color disabledHover = GRAY;

    bool isPressed;
    bool isHovered;
    bool wasMousePressed;
    float roundness = 5;

    Text text;

    Tooltip tooltip;
    bool showTooltip = false;
    float tooltipDelay = 0.75f; // Time in seconds before showing tooltip
    float tooltipTimer = 0.0f;


    bool isDisabled;
    
    Sound clickSound;

private:
    Rectangle bounds;
    void (*onClick)();

    bool transitioningHover;
    bool transitioningUnhover;
    float hoverTransitionTime;
    float unhoverTransitionTime;
    float transitionDuration = 0.075f;
    Color startColorHover;
    Color targetColorHover;
    Color startColorUnhover;
    Color targetColorUnhover;
    Color btnColor = color;

public:
    LitButton(Vector3 position = {0,0,0}, Vector2 size={100, 50}, Color color = LIGHTGRAY, Color pressedColor = DARKGRAY, Color hoverColor = GRAY, Text text = Text())
        : position(position), size(size), color(color), pressedColor(pressedColor), hoverColor(hoverColor), text(text),
        isPressed(false), isHovered(false), isDisabled(isDisabled), bounds({ position.x, position.y, size.x, size.y }), onClick(nullptr), wasMousePressed(false),
        transitioningHover(false), transitioningUnhover(false), tooltip(), clickSound(LoadSound("click.wav")) // Initialize the tooltip and sound
    {
    }

    void SetText(const char* button_text, float fontSize = 20, Color color = BLACK) {
        text.text = button_text;
        text.fontSize = fontSize;
        text.color = color;
        text.position = { position.x + size.x / 2 - MeasureText(button_text, fontSize) / 2, position.y + size.y / 2 - fontSize / 2, 0 };
    }

    void SetTooltip(const char* tooltipText, Font font = GetFontDefault(), Color backgroundColor = GRAY, Color textColor = WHITE) {
        tooltip.text = tooltipText;
        tooltip.position = { position.x + size.x, position.y };
        tooltip.size = { MeasureText(tooltipText, font.baseSize) + 10, font.baseSize + 10 };
        tooltip.font = font;
        tooltip.backgroundColor = backgroundColor;
        tooltip.textColor = textColor;
    }

    

    void SetOnClickCallback(void (*callback)()) {
        onClick = callback;
    }


    void PlayClickSound() {
        if (!isDisabled) {
            PlaySound(clickSound);
        }
    }

    void Update() {
        isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);
        bool isMousePressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
        
        if (isDisabled)
        {
            btnColor = disabledButton;
            text.color = disabledText;
        }

        if (!isDisabled && isHovered && !isPressed && isMousePressed && !wasMousePressed) {
            isPressed = true;
            btnColor = pressedColor;
            if (onClick)
                onClick();

            PlayClickSound();
        } else {
            isPressed = false;
        }


        if (isHovered && !isDisabled) {
            tooltipTimer += GetFrameTime();
            if (tooltipTimer >= tooltipDelay && !showTooltip) {
                showTooltip = true;
            }
        } else {
            tooltipTimer = 0.0f;
            showTooltip = false;
        }

        wasMousePressed = isMousePressed;
    }


    void Draw() {
        Update();

        // Smooth color transition for hovering
        if (!isDisabled && isHovered && !transitioningHover && !transitioningUnhover) {
            transitioningHover = true;
            hoverTransitionTime = 0;
            startColorHover = color;
            targetColorHover = hoverColor;
        } else if (!isDisabled && !isHovered && transitioningHover && !transitioningUnhover) {
            transitioningHover = false;
            hoverTransitionTime = 0;
            startColorHover = hoverColor;
            targetColorHover = color;
        }

        // Smooth color transition for unhovering
        if (!isDisabled && !isHovered && !transitioningHover && !transitioningUnhover) {
            transitioningUnhover = true;
            unhoverTransitionTime = 0;
            startColorUnhover = hoverColor;
            targetColorUnhover = color;
        } else if (!isDisabled && isHovered && !transitioningHover && transitioningUnhover) {
            transitioningUnhover = false;
            unhoverTransitionTime = 0;
            startColorUnhover = color;
            targetColorUnhover = hoverColor;
        }

        // Apply the appropriate color depending on the transition state
        if (!isDisabled && transitioningHover) {
            hoverTransitionTime += GetFrameTime();
            float t = hoverTransitionTime / transitionDuration;
            if (t > 1.0f) t = 1.0f;

            btnColor = LerpColor(startColorHover, targetColorHover, t);
        } else if (transitioningUnhover) {
            unhoverTransitionTime += GetFrameTime();
            float t = unhoverTransitionTime / transitionDuration;
            if (t > 1.0f) t = 1.0f;

            btnColor = LerpColor(startColorUnhover, targetColorUnhover, t);
        } else {
            btnColor = color;
        }

        DrawRectangleRounded(bounds, roundness/10, roundness/10, btnColor);

        DrawTextElement(text);

        if (showTooltip) {
            tooltip.position = GetMousePosition();
            tooltip.Draw();
        }
    }
};

std::vector<LitButton> lit_buttons;




LitButton& AddButton(const char* button_text, Vector3 position, Vector2 size)
{
    lit_buttons.emplace_back();
    LitButton& button = lit_buttons.back();
    button.text.text = button_text;
    button.position = position;
    button.size = size;
    return button;
}

void DrawButtons()
{

    for (const auto& button : lit_buttons)
    {
        button.Draw();
    }
}


std::string displayText = "Hi there. Thank you so much for using Lit Engine!\nThis is an example of a multiline text element.\nIt's a great way to display text in your game.\nYou can also use a custom font and color.\nHave fun!";
Text *MyTextElement = &AddText(displayText.c_str(), { 100, 100, 1 }, 20, BLUE);
int text_index = 0;
void OnButtonClick() {
    text_index++;
    if (text_index == 1)
    {
        displayText = "You can display text dynamically.\nYou can also use a custom font and color.\n\nWAIT??? IS THIS TEXT BIGGER?\nWell, yes. I changed the fontSize!";
        MyTextElement->text = displayText;
        MyTextElement->fontSize = 30;
        MyTextElement->color = RED;
    }
    else if (text_index == 2)
    {
        displayText = "You can also add\na background to your text,\neven rounded!";
        MyTextElement->text = displayText;
        MyTextElement->fontSize = 35;
        MyTextElement->color = GRAY;
        MyTextElement->backgroundColor = DARKGRAY;
        MyTextElement->backgroundRoundiness = 5;
        MyTextElement->position.x -= 40;
    }
}

int main(void)
{
    const int screenWidth = 800;
    const int screenHeight = 450;

    InitWindow(screenWidth, screenHeight, "GUI");
    InitAudioDevice();

    // AddText("FRONT", { 100, 100, 1 }, 40, GREEN);
    // AddText("ABOVE", { 100, 100, 2 }, 40, YELLOW);
    // AddText("BOTTOM", { 100, 100, 0 }, 20, RED);


    Vector3 buttonPosition = { 0, 0, 1 };
    Vector2 buttonSize = { 200, 50 };
    LitButton button = LitButton(buttonPosition, buttonSize);
    button.SetOnClickCallback(OnButtonClick);
    button.SetText("Click me!");
    button.SetTooltip("This is a tooltip!");

    lit_buttons.push_back(button);
    // LitButton button = AddButton("Click me!", { 100, 100, 1 }, { 200, 50 });
    // button.SetOnClickCallback(OnButtonClick);
    // button.SetTooltip("This is a tooltip!");

    while (!WindowShouldClose())
    {
        BeginDrawing();

            ClearBackground(RAYWHITE);

            DrawTextElements();
            DrawButtons();
            
        EndDrawing();
    }


    CloseWindow();
    CloseAudioDevice();
 
    return 0;
}