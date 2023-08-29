#include "../../../include_all.h"
#include "Button.h"

Color LerpColor(Color startColor, Color endColor, float t)
{
    unsigned char r = static_cast<unsigned char>(startColor.r + (endColor.r - startColor.r) * t);
    unsigned char g = static_cast<unsigned char>(startColor.g + (endColor.g - startColor.g) * t);
    unsigned char b = static_cast<unsigned char>(startColor.b + (endColor.b - startColor.b) * t);
    unsigned char a = static_cast<unsigned char>(startColor.a + (endColor.a - startColor.a) * t);

    return {r, g, b, a};
}

class LitButton
{
public:
    string name = "Button";
    LitVector3 position = {0, 0, 0};
    Vector2 size = {600, 450};
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

    bool autoResize = false;

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
    Vector2 renderTexturePos;

public:
    // Constructor for non-shipping mode
#ifndef GAME_SHIPPING
    LitButton(Vector3 position = {0, 0, 1}, Vector2 size = {600, 450}, Color color = LIGHTGRAY, Color pressedColor = DARKGRAY, Color hoverColor = GRAY, Text text = Text())
        : position(position), size(size), color(color), pressedColor(pressedColor), hoverColor(hoverColor), text(text),
          isPressed(false), isHovered(false), isDisabled(isDisabled), bounds({position.x, position.y, size.x, size.y}),
          onClick(nullptr), wasMousePressed(false), transitioningHover(false), transitioningUnhover(false), tooltip(), clickSound(LoadSound("click.wav")), renderTexturePos({rectangle.x, rectangle.y})
    {
    }

// Constructor for shipping mode
#else
    LitButton(Vector3 position = {0, 0, 1}, Vector2 size = {600, 450}, Color color = LIGHTGRAY, Color pressedColor = DARKGRAY, Color hoverColor = GRAY, Text text = Text())
        : position(position), size(size), color(color), pressedColor(pressedColor), hoverColor(hoverColor), text(text),
          isPressed(false), isHovered(false), isDisabled(isDisabled), bounds({position.x, position.y, size.x, size.y}),
          onClick(nullptr), wasMousePressed(false), transitioningHover(false), transitioningUnhover(false), tooltip(), clickSound(LoadSound("click.wav"))
    {
    }
#endif

    void SetText(const char *button_text, float fontSize = 20, Color color = BLACK)
    {
        text.text = button_text;
        text.fontSize = fontSize;
        text.color = color;
        text.position = {position.x + size.x / 2 - MeasureText(button_text, fontSize) / 2, position.y + size.y / 2 - fontSize / 2, 0};
    }

    void SetTooltip(const char *tooltipText, Font font = GetFontDefault(), Color backgroundColor = GRAY, Color textColor = WHITE)
    {
        tooltip.text = tooltipText;
        tooltip.position = {position.x + size.x, position.y};
        tooltip.size = {MeasureText(tooltipText, font.baseSize) + 10, font.baseSize + 10};
        tooltip.font = font;
        tooltip.backgroundColor = backgroundColor;
        tooltip.textColor = textColor;
    }

    void SetOnClickCallback(void (*callback)())
    {
        onClick = callback;
    }

    void PlayClickSound()
    {
        if (!isDisabled)
        {
            PlaySound(clickSound);
        }
    }

    void Update()
    {

#ifndef GAME_SHIPPING
        Vector2 mainMousePos = GetMousePosition();
        Vector2 renderTexturePos = {rectangle.x, rectangle.y};
        Vector2 renderTextureSize = {rectangle.width, rectangle.height};
        Vector2 relativeMousePos = Vector2Subtract(mainMousePos, renderTexturePos);
        Vector2 renderTextureMousePos = relativeMousePos;

        float divideX = GetScreenWidth() / ImGui::GetWindowSize().x;
        float divideY = GetScreenHeight() / ImGui::GetWindowSize().y;

        Vector2 position = {bounds.x - ImGui::GetFrameHeight(), bounds.y};

        isHovered = CheckCollisionPointRec(renderTextureMousePos, {position.x/divideX, position.y/divideY, bounds.width/divideX, bounds.height/divideY});
#else
        isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);
#endif
        bool isMousePressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

        if (isDisabled)
        {
            btnColor = disabledButton;
            text.color = disabledText;
        }

        if (!isDisabled && isHovered && !isPressed && isMousePressed && !wasMousePressed)
        {
            isPressed = true;
            btnColor = pressedColor;
            if (onClick)
                onClick();

            PlayClickSound();
        }
        else
        {
            isPressed = false;
        }

#ifdef GAME_SHIPPING

        if (isHovered && !isDisabled)
        {
            tooltipTimer += GetFrameTime();
            if (tooltipTimer >= tooltipDelay && !showTooltip)
            {
                showTooltip = true;
            }
        }
        else
        {
            tooltipTimer = 0.0f;
            showTooltip = false;
        }
#endif
        wasMousePressed = isMousePressed;
    }

    void Draw()
    {
        Update();

        // Check if not in any transition
        bool notInTransition = !transitioningHover && !transitioningUnhover;

        // Initialize start colors if transitioning just started
        if (notInTransition)
        {
            startColorHover = btnColor;
            startColorUnhover = btnColor;
        }

        // Smooth color transition for hovering
        if (isHovered && !transitioningHover)
        {
            transitioningHover = true;
            hoverTransitionTime = 0;
            targetColorHover = hoverColor;
        }
        else if (!isHovered && transitioningHover)
        {
            transitioningHover = false;
            hoverTransitionTime = 0;
            targetColorHover = startColorHover;
        }

        // Smooth color transition for unhovering
        if (!isHovered && notInTransition)
        {
            transitioningUnhover = true;
            unhoverTransitionTime = 0;
            targetColorUnhover = color;
        }
        else if (isHovered && transitioningUnhover)
        {
            transitioningUnhover = false;
            unhoverTransitionTime = 0;
            targetColorUnhover = startColorUnhover;
        }

        // Apply the appropriate color depending on the transition state
        if (transitioningHover)
        {
            hoverTransitionTime += GetFrameTime();
            float t = hoverTransitionTime / transitionDuration;
            if (t > 1.0f)
            {
                t = 1.0f;
                transitioningHover = false;
            }
            btnColor = LerpColor(startColorHover, targetColorHover, t);
        }
        else if (transitioningUnhover)
        {
            unhoverTransitionTime += GetFrameTime();
            float t = unhoverTransitionTime / transitionDuration;
            if (t > 1.0f)
            {
                t = 1.0f;
                transitioningUnhover = false;
            }
            btnColor = LerpColor(startColorUnhover, targetColorUnhover, t);
        }

        text.position = {position.x + size.x / 2 - MeasureText(text.text.c_str(), text.fontSize) / 2, position.y + size.y / 2 - text.fontSize / 2, 0};
        
        if (autoResize)
            DrawRectangleRounded(text.bounds, roundness / 10, roundness / 10, btnColor);
        else
        {
            bounds = {
                position.x,
                position.y,
                size.x,
                size.y
            };

            DrawRectangleRounded(bounds, roundness / 10, roundness * 10, btnColor);
        }

        text.Draw();

        if (showTooltip)
        {
            tooltip.position = GetMousePosition();
            tooltip.Draw();
        }
    }

};


void print_hi()
{
    std::cout << "Pressed\n";
}

LitButton &AddButton(const char* button_text, Vector3 position, Vector2 size, float text_size = 20)
{
    LitButton button = LitButton(position, size);
    button.SetText(button_text, text_size);
    button.SetTooltip("This is a tooltip!");
    button.SetOnClickCallback(print_hi);

    lit_buttons.push_back(button);
}

void DrawButtons()
{

    for (const auto &button : lit_buttons)
    {
        button.Draw();
    }
}