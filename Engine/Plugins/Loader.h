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
void checkPluginsConfigIntegrity();

class Plugin {
public:
    std::string m_name;
    std::string m_path;

    Plugin(const std::string& name, const std::string& path) : m_name(name), m_path(path) {}

    const std::string& getName() const { return m_name; }
    const std::string& getPath() const { return m_path; }

    virtual void load() = 0;
    virtual void unload() = 0;
    virtual void reload() = 0;

    virtual ~Plugin() {}
};

class Plugins {
public:
    Plugins() = default;
    ~Plugins() = default;

    void load(const std::string& name, const std::string& path);
    void unload(const std::string& name);
    void reload(const std::string& name);
    void reloadAll();
    void unloadAll();
    bool isPluginLoaded(const std::string& name) const;

private:
    std::unordered_map<std::string, std::shared_ptr<Plugin>> m_plugins;
};

#endif // PLUGINS_LOADER_H
