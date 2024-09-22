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
    if (!fs::exists(".profile/")) {
        TraceLog(LOG_WARNING, "No .profile/ directory found! Creating it.");
        fs::create_directory(".profile/");
    }

    if (!fs::exists(".profile/plugins.json")) {
        TraceLog(LOG_WARNING, "No plugins.json found in .profile! Creating an empty file.");
        std::ofstream file(".profile/plugins.json");
        // Write empty JSON structure
        json emptyPlugins = { {"plugins", json::object()} };
        file << emptyPlugins.dump(4);
        file.close();
        return;
    }

    std::ifstream file(".profile/plugins.json");
    json pluginsConfig;
    try {
        file >> pluginsConfig; // Attempt to parse the JSON
    } catch (json::parse_error& e) {
        TraceLog(LOG_ERROR, ".profile/plugins.json is not valid JSON!");
        return;
    }

    if (!pluginsConfig.contains("plugins") || !pluginsConfig["plugins"].is_object()) {
        TraceLog(LOG_WARNING, "Invalid structure in .profile/plugins.json. Expecting 'plugins' object.");
        return;
    }

    // Check if all pluginPaths exist
    for (const auto& [pluginName, pluginData] : pluginsConfig["plugins"].items()) {
        if (!pluginData.contains("path") || !pluginData["path"].is_string()) {
            std::string message = "Invalid plugin configuration for '" + pluginName + "'. 'path' field is missing or not a string.";
            TraceLog(LOG_WARNING, message.c_str());
            continue; // Skip to next plugin
        }

        std::string pluginPath = pluginData["path"];

        // Check if pluginPath exists
        if (!fs::exists(pluginPath)) {
            std::string message = "Plugin Path '" + pluginPath + "' for '" + pluginName + "' does not exist.";
            TraceLog(LOG_WARNING, message.c_str());

            continue; // Skip further checks for this plugin
        }

        // Check if pluginName is in pluginPath (example: looking for a main.py file for a Python plugin)
        std::string pluginMainFile = pluginPath + "/main.py";
        if (!fs::exists(pluginMainFile)) {
            std::string message = "Plugin '" + pluginName + "' main file not found at '" + pluginMainFile + "'.";
            TraceLog(LOG_WARNING, message.c_str());
        } else {
            std::string message = "Plugin '" + pluginName + "' is valid.";
            TraceLog(LOG_DEBUG, message.c_str());
        }
    }

    // Check if .profile/plugins/ directory exists
    if (!fs::exists(".profile/plugins/")) {
        TraceLog(LOG_WARNING, "No plugins directory found at .profile/plugins/! Creating it.");
        fs::create_directory(".profile/plugins/");
    }
}
