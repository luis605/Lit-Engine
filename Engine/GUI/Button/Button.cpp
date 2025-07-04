/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Button.hpp"

#define RAYMATH_IMPLEMENTATION
#include <raylib.h>
#include <raymath.h>

std::vector<LitButton> litButtons;

Color LerpColor(Color startColor, Color endColor, float t) {
    unsigned char r = static_cast<unsigned char>(
        startColor.r + (endColor.r - startColor.r) * t);
    unsigned char g = static_cast<unsigned char>(
        startColor.g + (endColor.g - startColor.g) * t);
    unsigned char b = static_cast<unsigned char>(
        startColor.b + (endColor.b - startColor.b) * t);
    unsigned char a = static_cast<unsigned char>(
        startColor.a + (endColor.a - startColor.a) * t);

    return {r, g, b, a};
}

void LitButton::SetText(const char* button_text, float fontSize, Color color) {
    text.text = button_text;
    text.fontSize = fontSize;
    text.color = color;
    text.position = {position.x + size.x / 2 -
                         MeasureText(button_text, fontSize) / 2,
                     position.y + size.y / 2 - fontSize / 2, 0};
}

void LitButton::SetTooltip(const char* tooltipText, Font font,
                           Color backgroundColor, Color textColor) {
    tooltip.text = tooltipText;
    tooltip.position = {position.x + size.x, position.y};
    tooltip.size = {MeasureText(tooltipText, font.baseSize) + 10.0f,
                    font.baseSize + 10.0f};
    tooltip.font = font;
    tooltip.backgroundColor = backgroundColor;
    tooltip.textColor = textColor;
}

void LitButton::SetOnClickCallback(void (*callback)()) { onClick = callback; }

void LitButton::PlayClickSound() {
    if (enabled && IsSoundValid(clickSound)) {
        PlaySound(clickSound);
    }
}

void LitButton::Update() {

#ifndef GAME_SHIPPING
    Vector2 mainMousePos = GetMousePosition();
    Vector2 renderTexturePos = {viewportRectangle.x, viewportRectangle.y};
    Vector2 renderTextureSize = {viewportRectangle.width,
                                 viewportRectangle.height};
    Vector2 relativeMousePos = Vector2Subtract(mainMousePos, renderTexturePos);
    Vector2 renderTextureMousePos = relativeMousePos;

    float divideX = GetScreenWidth() / ImGui::GetWindowSize().x;
    float divideY = GetScreenHeight() / ImGui::GetWindowSize().y;

    Vector2 position = {bounds.x - ImGui::GetFrameHeight(), bounds.y};

    isHovered = CheckCollisionPointRec(
        renderTextureMousePos,
        {position.x / divideX, position.y / divideY, bounds.width / divideX,
         bounds.height / divideY});
#else
    isHovered = CheckCollisionPointRec(GetMousePosition(), bounds);
#endif
    bool isMousePressed = IsMouseButtonDown(MOUSE_LEFT_BUTTON);

    if (!enabled) {
        btnColor = disabledButtonColor;
        text.color = disabledText;
    }

    if (enabled && isHovered && !isPressed && isMousePressed &&
        !wasMousePressed) {
        isPressed = true;
        btnColor = pressedColor;
        if (onClick)
            onClick();

        PlayClickSound();
    } else {
        isPressed = false;
    }

#ifdef GAME_SHIPPING

    if (isHovered && enabled) {
        tooltipTimer += GetFrameTime();
        if (tooltipTimer >= tooltipDelay && !showTooltip) {
            showTooltip = true;
        }
    } else {
        tooltipTimer = 0.0f;
        showTooltip = false;
    }
#endif
    wasMousePressed = isMousePressed;
}

void LitButton::Draw() {
    Update();

    // Check if not in any transition
    bool notInTransition = !transitioningHover && !transitioningUnhover;

    // Initialize start colors if transitioning just started
    if (notInTransition) {
        if (enabled) {
            startColorHover = btnColor;
            startColorUnhover = btnColor;
        } else {
            startColorHover = disabledButtonColor;
            startColorUnhover = disabledButtonColor;
        }
    }

    // Smooth color transition for hovering
    if (isHovered && !transitioningHover) {
        transitioningHover = true;
        hoverTransitionTime = 0;
        if (enabled)
            targetColorHover = hoverColor;
        else
            targetColorHover = disabledHoverColor;
        ;

    } else if (!isHovered && transitioningHover) {
        transitioningHover = false;
        hoverTransitionTime = 0;
        targetColorHover = startColorHover;
    }

    // Smooth color transition for unhovering
    if (!isHovered && notInTransition) {
        transitioningUnhover = true;
        unhoverTransitionTime = 0;
        if (enabled)
            targetColorUnhover = color;
        else
            targetColorUnhover = disabledButtonColor;
    } else if (isHovered && transitioningUnhover) {
        transitioningUnhover = false;
        unhoverTransitionTime = 0;
        targetColorUnhover = startColorUnhover;
    }

    // Apply the appropriate color depending on the transition state
    if (transitioningHover) {
        hoverTransitionTime += GetFrameTime();
        float t = hoverTransitionTime / transitionDuration;
        if (t > 1.0f) {
            t = 1.0f;
            transitioningHover = false;
        }
        btnColor = LerpColor(startColorHover, targetColorHover, t);
    } else if (transitioningUnhover) {
        unhoverTransitionTime += GetFrameTime();
        float t = unhoverTransitionTime / transitionDuration;
        if (t > 1.0f) {
            t = 1.0f;
            transitioningUnhover = false;
        }
        btnColor = LerpColor(startColorUnhover, targetColorUnhover, t);
    }

    text.position = {position.x + size.x / 2 -
                         MeasureText(text.text.c_str(), text.fontSize) / 2,
                     position.y + size.y / 2 - text.fontSize / 2, 0};

    if (autoResize)
        DrawRectangleRounded(text.bounds, roundness / 10, roundness / 10,
                             btnColor);
    else {
        bounds = {position.x, position.y, size.x, size.y};

        DrawRectangleRounded(bounds, roundness / 10, roundness * 10, btnColor);
    }

    text.Draw();

    if (showTooltip) {
        tooltip.position = GetMousePosition();
        tooltip.Draw();
    }
}

LitButton& AddButton(const char* button_text, Vector3 position, Vector2 size,
                     float text_size) {
    LitButton button = LitButton(position, size);
    button.SetText(button_text, text_size);
    button.SetTooltip("This is a tooltip!");

    litButtons.emplace_back(button);
    return litButtons.back();
}

void DrawButtons() {
    for (LitButton& button : litButtons) {
        button.Draw();
    }
}