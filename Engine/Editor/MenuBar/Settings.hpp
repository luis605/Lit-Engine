/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#ifndef _SETTINGS_H_
#define _SETTINGS_H_

extern bool showSettingsWindow;
extern bool autoSaveCode;
extern bool showCodeLineNumbers;
extern bool gridSnappingEnabled;
extern bool vsyncEnabled;

extern float editorFontSize;
extern float gridSnappingFactor;

enum class SettingsCategory {
    Profile,
    Code,
    Editor,
    Rendering,
    Audio,
    Controls,
    Plugins
};

void Settings();

#endif // _SETTINGS_H_