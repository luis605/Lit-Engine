#ifndef LITBUTTONS_H
#define LITBUTTONS_H

#include <Engine/Core/global_variables.hpp>
#include <Engine/GUI/Text/Text.hpp>
#include <Engine/GUI/Tooltip/Tooltip.hpp>
#include <string>
#include <vector>

class LitButton {
  public:
    std::string name = "Button";
    LitVector3 position = {0, 0, 0};
    Vector2 size = {600, 450};
    Color color = LIGHTGRAY;
    Color pressedColor = DARKGRAY;
    Color hoverColor = GRAY;
    Color disabledButtonColor = GRAY;
    Color disabledText = DARKGRAY;
    Color disabledHoverColor = GRAY;

    bool isPressed;
    bool isHovered;
    bool wasMousePressed;
    bool enabled = true;
    bool showTooltip = false;
    bool autoResize = false;
    float roundness = 5;
    float tooltipDelay = 0.75f;
    float tooltipTimer = 0.0f;

    Text text;
    Tooltip tooltip;
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
    Vector2 renderTexturePos;

  public:
#ifndef GAME_SHIPPING
    LitButton(Vector3 position = {0, 0, 1}, Vector2 size = {600, 450},
              Color color = LIGHTGRAY, Color pressedColor = DARKGRAY,
              Color hoverColor = GRAY, Text text = Text())
        : position(position), size(size), color(color),
          pressedColor(pressedColor), hoverColor(hoverColor), text(text),
          isPressed(false), isHovered(false), enabled(enabled),
          bounds({position.x, position.y, size.x, size.y}), onClick(nullptr),
          wasMousePressed(false), transitioningHover(false),
          transitioningUnhover(false), tooltip(),
          clickSound(LoadSound("click.wav")),
          renderTexturePos({viewportRectangle.x, viewportRectangle.y}) {}

// Constructor for shipping mode
#else
    LitButton(Vector3 position = {0, 0, 1}, Vector2 size = {600, 450},
              Color color = LIGHTGRAY, Color pressedColor = DARKGRAY,
              Color hoverColor = GRAY, Text text = Text())
        : position(position), size(size), color(color),
          pressedColor(pressedColor), hoverColor(hoverColor), text(text),
          isPressed(false), isHovered(false), enabled(enabled),
          bounds({position.x, position.y, size.x, size.y}), onClick(nullptr),
          wasMousePressed(false), transitioningHover(false),
          transitioningUnhover(false), tooltip(),
          clickSound(LoadSound("click.wav")) {}
#endif

    void SetText(const char* button_text, float fontSize = 20,
                 Color color = BLACK);
    void SetTooltip(const char* tooltipText, Font font = GetFontDefault(),
                    Color backgroundColor = GRAY, Color textColor = WHITE);
    void SetOnClickCallback(void (*callback)());
    void PlayClickSound();
    void Update();
    void Draw();
};

extern std::vector<LitButton> litButtons;
void DrawButtons();
LitButton& AddButton(const char* button_text, Vector3 position, Vector2 size,
                     float text_size = 20);

#endif // LITBUTTONS_H