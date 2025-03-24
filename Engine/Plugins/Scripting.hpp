#ifndef PLUGINS_SCRIPTING_H
#define PLUGINS_SCRIPTING_H

#include <Engine/Scripting/math.hpp>
#include <raylib.h>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <filesystem>
#include <functional>
#include <string>

namespace py = pybind11;
namespace fs = std::filesystem;

void initWindow(std::string& windowName, int width, int height, bool resizable);
void closeWindow();
void drawText(std::string& text, int x = -1, int y = -1, LitVector3 color = LitVector3(255.0f, 255.0f, 255.0f));
bool drawButton(std::string& text, int x = -1, int y = -1, int width = -1, int height = -1, LitVector3 buttonColor = LitVector3(-1, -1, -1), LitVector3 textColor = LitVector3(-1, -1, -1));
void setSkybox(const std::string& skyboxPath);
void onEntityCreation(const std::string& listenerName, const std::function<void()>& callback);
void onEntityDestruction(const std::string& listenerName, const std::function<void()>& callback);
void onSceneSave(const std::string& listenerName, const std::function<void()>& callback);
void onSceneLoad(const std::string& listenerName, const std::function<void()>& callback);
void onScenePlay(const std::string& listenerName, const std::function<void()>& callback);
void onSceneStop(const std::string& listenerName, const std::function<void()>& callback);
void createEvent(const std::string& eventName);
void onCustomEvent(const std::string& eventName, const std::function<void()>& callback);
void triggerCustomEvent(const std::string& eventName);

#endif // PLUGINS_SCRIPTING_H
