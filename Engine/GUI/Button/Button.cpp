#include "../../../include_all.h"
#include "Button.h"

Color LerpColor(Color startColor, Color endColor, float t)
{
    unsigned char r = static_cast<unsigned char>(startColor.r + (endColor.r - startColor.r) * t);
    unsigned char g = static_cast<unsigned char>(startColor.g + (endColor.g - startColor.g) * t);
    unsigned char b = static_cast<unsigned char>(startColor.b + (endColor.b - startColor.b) * t);
    unsigned char a = static_cast<unsigned char>(startColor.a + (endColor.a - startColor.a) * t);

    return { r, g, b, a };
}

class LitButton {
public:
    Vector3 position = { 0, 0, 0 };
    Vector2 size = { 600, 450 };
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
    LitButton(Vector3 position = { 0, 0, 1 }, Vector2 size = { 600, 450 }, Color color = LIGHTGRAY, Color pressedColor = DARKGRAY, Color hoverColor = GRAY, Text text = Text())
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
        std::cout << button.text.text << std::endl;
        button.Draw();
    }
}