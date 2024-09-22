#include "Loader.h"

void loadPlugin(const std::string& pluginName) {
    // Example implementation of loading a plugin
    std::cout << "LOADING: " << pluginName << std::endl;
}

void loadAllPlugins() {
    if (!checkPluginsConfigIntegrity()) {
        TraceLog(LOG_ERROR, "Plugins configuration integrityPassed check failed. Could not load plugins.");
        return;
    }

    std::ifstream file(".profile/plugins.json");
    json pluginsConfig;

    try {
        file >> pluginsConfig;
    } catch (json::parse_error& e) {
        TraceLog(LOG_ERROR, "Failed to parse plugins.json");
        return;
    }

    for (const auto& [pluginName, pluginData] : pluginsConfig["plugins"].items()) {
        if (!pluginData.contains("path") || !pluginData["path"].is_string()) {
            TraceLog(LOG_WARNING, ("Plugin '" + pluginName + "' is missing a valid 'path' field. Skipping.").c_str());
            continue;
        }

        std::string pluginPath = pluginData["path"];

        if (!fs::exists(pluginPath)) {
            TraceLog(LOG_WARNING, ("Plugin path '" + pluginPath + "' for '" + pluginName + "' does not exist. Skipping.").c_str());
            continue;
        }

        try {
            loadPlugin(pluginName);
        } catch (const std::exception& e) {
            std::string message = "Failed to load plugin '" + pluginName + "': " + e.what();
            TraceLog(LOG_ERROR, message.c_str());
        }
    }
}

void unloadAllPlugins() {
    // Unload all plugins using Plugins.unloadAll();
}

bool checkPluginsConfigIntegrity() {
    bool integrityPassed = true;

    if (!fs::exists(".profile/")) {
        TraceLog(LOG_WARNING, "No .profile/ directory found! Creating it.");
        fs::create_directory(".profile/");
        integrityPassed = false;
    }

    if (!fs::exists(".profile/plugins.json")) {
        TraceLog(LOG_WARNING, "No plugins.json found in .profile! Creating an empty file.");
        std::ofstream file(".profile/plugins.json");
        // Write empty JSON structure
        json emptyPlugins = { {"plugins", json::object()} };
        file << emptyPlugins.dump(4);
        file.close();

        integrityPassed = false;
        return integrityPassed;
    }

    std::ifstream file(".profile/plugins.json");
    json pluginsConfig;
    try {
        file >> pluginsConfig; // Attempt to parse the JSON
    } catch (json::parse_error& e) {
        TraceLog(LOG_ERROR, ".profile/plugins.json is not valid JSON!");
        integrityPassed = false;
        return integrityPassed;
    }

    if (!pluginsConfig.contains("plugins") || !pluginsConfig["plugins"].is_object()) {
        TraceLog(LOG_WARNING, "Invalid structure in .profile/plugins.json. Expecting 'plugins' object.");
        integrityPassed = false;
        return integrityPassed;
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

        integrityPassed = false;
        return integrityPassed;
    }

    return integrityPassed;
}
