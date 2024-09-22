#include "Loader.h"

void loadPlugin(const std::string& pluginName) {
    // Search .profile/plugins.json for pluginName
    // If found, load pluginPath
    // If not found, log warning
}

void unloadPlugin(const std::string& pluginName) {
    // Search for pluginName in Plugins, using Plugins.isPluginLoaded();
    // If found, unload plugin
    // If not found, log warning
}

void loadAllPlugins() {
    // Load all plugins in .profile/plugins.json
}

void unloadAllPlugins() {
    // Unload all plugins using Plugins.unloadAll();
}

void checkPluginsConfigIntegrity() {
    // Check if .profile/plugins.json exists
    // If not, log warning
    // If exists, check if .profile/plugins.json is valid json
    // If not, log warning
    // If valid json, check if all pluginPaths exist
    // If not, log warning
    // If all pluginPaths exist, check if all pluginNames are in pluginPaths
    // If not, log warning
    // If all pluginNames are in pluginPaths, check if all pluginNames are loaded
    // If not, log warning
    // Check if .profile/plugins/ exists
    // If not, log warning
}