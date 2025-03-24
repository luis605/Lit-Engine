#ifndef MENU_BAR_H
#define MENU_BAR_H

constexpr float BUTTON_PADDING = 30.0f;
constexpr float BUTTON_HEIGHT  = 30.0f;
constexpr ImVec2 BUTTON_SIZE   = ImVec2(120.0f, BUTTON_HEIGHT);
extern bool showAppearanceWindow;
extern bool showDebugWindow;
extern bool menuButtonClicked;

void Appearance();
void DebugWindow();
void DrawMenus();
void openAboutPage();
void openManualPage();
void MenuBar();

#endif // MENU_BAR_H