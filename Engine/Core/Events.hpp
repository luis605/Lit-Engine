#ifndef EVENTS_H
#define EVENTS_H

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <memory>

class ConcreteListener {
public:
    ConcreteListener(const std::string& name)
        : m_name(name) {}

    void addCallback(const std::function<void()>& callback);
    void onEvent();

public:
    std::string m_name;
    std::function<void()> m_callback;
};

class EventSource {
public:
    void addListener(const ConcreteListener& listener);
    void removeListener(const std::string& listenerId);
    void triggerEvent();

private:
    std::vector<ConcreteListener> m_listeners;
};

class EventManager {
public:
    void createEvent(const std::string& eventName);
    EventSource* getEvent(const std::string& eventName);

public:
    EventSource onEntityCreation;
    EventSource onEntityDestruction;
    EventSource onSceneSave;
    EventSource onSceneLoad;
    EventSource onScenePlay;
    EventSource onSceneStop;

private:
    std::unordered_map<std::string, EventSource> customEvents;
};

// For scripts (plugins, game scripts, etc.)
void createEvent(const std::string& eventName);
void onCustomEvent(const std::string& eventName, const std::function<void()>& callback);
void triggerCustomEvent(const std::string& eventName);
void onEntityCreation(const std::string& listenerName, const std::function<void()>& callback);
void onEntityDestruction(const std::string& listenerName, const std::function<void()>& callback);

extern EventManager eventManager;

#endif // EVENTS_H