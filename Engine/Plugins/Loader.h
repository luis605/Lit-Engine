#ifndef PLUGINS_LOADER_H
#define PLUGINS_LOADER_H

#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

void loadPlugin(const std::string& pluginName);
void unloadPlugin(const std::string& pluginName);
void loadAllPlugins();
void unloadAllPlugins();
bool checkPluginsConfigIntegrity();

class Plugin {
public:
    bool        m_enabled;
    std::string m_name;
    std::string m_path;
    py::object  m_module;
    fs::file_time_type m_lastWriteTime;

    Plugin(const std::string& name, const std::string& path, const bool& enabled) : m_name(name), m_path(path), m_enabled(enabled) {}

    const std::string& getName() const { return m_name; }
    const std::string& getPath() const { return m_path; }

    void initialize();
    void update();
    void unload();
    void reload();
    void reloadIfChanged();
    void saveEnabledState();

    ~Plugin() {}
};

class Plugins {
public:
    Plugins() = default;
    ~Plugins() = default;

    void load(const std::string& name, const std::string& path, const bool& enabled);
    void unload(const std::string& name);
    void reload(const std::string& name);
    void reloadAll();
    void unloadAll();
    void updateAll();
    void initializeAllPlugins();
    bool isPluginLoaded(const std::string& name) const;
    std::unordered_map<std::string, std::shared_ptr<Plugin>>& getPlugins() { return m_plugins; }

private:
    std::unordered_map<std::string, std::shared_ptr<Plugin>> m_plugins;
};

Plugins pluginManager;

#endif // PLUGINS_LOADER_H
