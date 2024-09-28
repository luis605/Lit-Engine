#include "Loader.h"
#include "Scripting.h"

void Plugin::initialize() {
    try {
        py::module::import("sys").attr("path").attr("append")(m_path.c_str());
        py::module mathModule            = py::module::import("mathModule"); // Include LitVector3
        py::module pluginScriptingModule = py::module::import("pluginScriptingModule");
        py::module inputModule           = py::module::import("inputModule");
        m_module                         = py::module::import("main");

        py::dict locals = py::dict(
            "Vector3"_a              = mathModule.attr("Vector3"),
            "initWindow"_a           = pluginScriptingModule.attr("initWindow"),
            "closeWindow"_a          = pluginScriptingModule.attr("closeWindow"),
            "setSkybox"_a            = pluginScriptingModule.attr("setSkybox"),
            "drawText"_a             = pluginScriptingModule.attr("drawText"),
            "drawButton"_a           = pluginScriptingModule.attr("drawButton"),
            "onEntityCreation"_a     = pluginScriptingModule.attr("onEntityCreation"),
            "onEntityDestruction"_a  = pluginScriptingModule.attr("onEntityDestruction"),
            "onSceneSave"_a          = pluginScriptingModule.attr("onSceneSave"),
            "onSceneLoad"_a          = pluginScriptingModule.attr("onSceneLoad"),
            "onScenePlay"_a          = pluginScriptingModule.attr("onScenePlay"),
            "onSceneStop"_a          = pluginScriptingModule.attr("onSceneStop"),
            "IsMouseButtonPressed"_a = inputModule.attr("isMouseButtonPressed"),
            "IsKeyDown"_a            = inputModule.attr("isKeyDown"),
            "IsKeyPressed"_a         = inputModule.attr("isKeyPressed"),
            "IsKeyUp"_a              = inputModule.attr("isKeyUp"),
            "GetMouseMovement"_a     = inputModule.attr("getMouseMovement"),
            "Keys"_a                 = inputModule.attr("Keys"),
            "MouseButton"_a          = inputModule.attr("MouseButton")
        );

        for (auto item : locals) {
            m_module.attr(item.first) = item.second;
        }

        if (m_module.attr("initPlugin")) {
            m_module.attr("initPlugin")();
        } else {
            TraceLog(LOG_WARNING, ("Initialization function not found in plugin: " + m_name).c_str());
        }
    } catch (const py::error_already_set& e) {
        TraceLog(LOG_ERROR, ("Failed to initialize plugin '" + m_name + "': " + std::string(e.what())).c_str());
    }
}

void Plugin::update() {
    try {
        if (!m_module) return;

        if (m_module.attr("updatePlugin")) {
            m_module.attr("updatePlugin")();
        } else {
            TraceLog(LOG_WARNING, ("Update function not found in plugin: " + m_name).c_str());
        }
    } catch (const py::error_already_set& e) {
        TraceLog(LOG_ERROR, ("Failed to update plugin '" + m_name + "': " + std::string(e.what())).c_str());
    }
}

void Plugin::reload() {
    unload();
    initialize();
}

void Plugin::reloadIfChanged() {
    if (fs::last_write_time(m_path) > m_lastWriteTime) {
        reload();
        m_lastWriteTime = fs::last_write_time(m_path);
    }
}

void Plugin::unload() {
    try {
        if (m_module.attr("unloadPlugin")) {
            m_module.attr("unloadPlugin")();
        } else {
            TraceLog(LOG_WARNING, ("Unload function not found in plugin: " + m_name).c_str());
        }
    } catch (const py::error_already_set& e) {
        TraceLog(LOG_ERROR, ("Failed to unload plugin '" + m_name + "': " + std::string(e.what())).c_str());
    }
}

void Plugin::saveEnabledState() {
    std::ifstream inputFile(".profile/plugins.json");
    json pluginsConfig;

    try {
        inputFile >> pluginsConfig;
    } catch (json::parse_error& e) {
        TraceLog(LOG_ERROR, "Failed to parse plugins.json");
        return;
    }

    inputFile.close();

    for (auto& [pluginName, pluginData] : pluginsConfig["plugins"].items()) {
        if (pluginName == m_name) {
            pluginData["enabled"] = m_enabled;
            break;
        }
    }

    std::ofstream outputFile(".profile/plugins.json");
    if (!outputFile) {
        TraceLog(LOG_ERROR, "Failed to open plugins.json for writing");
        return;
    }

    outputFile << pluginsConfig.dump(4);
    outputFile.close();
}

void Plugins::load(const std::string& name, const std::string& path, const bool& enabled) {
    m_plugins.insert({name, std::make_unique<Plugin>(name, path, enabled)});
    TraceLog(LOG_INFO, ("Loaded plugin: " + name).c_str());
}

void Plugins::initializeAllPlugins() {
    for (const auto& [name, plugin] : m_plugins) {
        plugin->initialize();
    }
}

void Plugins::updateAll() {
    for (const auto& [name, plugin] : m_plugins) {
        if (plugin->m_enabled) plugin->update();
    }
}

void Plugins::unloadAll() {
    for (const auto& [name, plugin] : m_plugins) {
        plugin->unload();
    }
    m_plugins.clear();
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
        bool enabled = pluginData["enabled"];

        if (!fs::exists(pluginPath)) {
            TraceLog(LOG_WARNING, ("Plugin path '" + pluginPath + "' for '" + pluginName + "' does not exist. Skipping.").c_str());
            continue;
        }

        try {
            pluginManager.load(pluginName, pluginPath, enabled);
        } catch (const std::exception& e) {
            std::string message = "Failed to load plugin '" + pluginName + "': " + e.what();
            TraceLog(LOG_ERROR, message.c_str());
        }
    }

    pluginManager.initializeAllPlugins();
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
