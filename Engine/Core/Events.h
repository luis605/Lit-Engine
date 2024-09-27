#ifndef EVENTS_H
#define EVENTS_H

#include <string>
#include <iostream>
#include <vector>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <stdexcept>
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
    EventSource onEntityCreation;
};

extern EventManager eventManager;

#endif // EVENTS_H