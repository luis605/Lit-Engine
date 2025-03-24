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
    void addListener(ConcreteListener& listener);
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

extern EventManager eventManager;

#endif // EVENTS_H