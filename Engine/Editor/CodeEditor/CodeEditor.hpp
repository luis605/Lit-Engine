#ifndef CODEEDITOR_H
#define CODEEDITOR_H

#include <TextEditor.h>
#include <extras/IconsFontAwesome6.h>
#include <filesystem>
#include <raylib.h>

namespace fs = std::filesystem;

extern Texture2D saveTexture;
extern Texture2D hotReloadTexture;

extern std::string code;
extern fs::path codeEditorScriptPath;
extern TextEditor editor;

void CodeEditor();

#endif // CODEEDITOR_H
