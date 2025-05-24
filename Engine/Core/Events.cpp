/*
This file is licensed under the PolyForm Noncommercial License 1.0.0.
See the LICENSE file in the project root for full license information.
*/

#include "Events.hpp"
#include <functional>
#include <string>
#include <pybind11/embed.h>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>

namespace py = pybind11;

void EventSource::addListener(const ConcreteListener& listener) {
    m_listeners.push_back(listener);
}

void EventSource::removeListener(const std::string& name) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
                                     [&](const ConcreteListener& existingListener) {
                                         return existingListener.m_name == name;
                                     }), m_listeners.end());}

void EventSource::triggerEvent() {
    for (auto listener : m_listeners) {
        listener.onEvent();
        listener.m_callback();
    }
}

void ConcreteListener::addCallback(const std::function<void()>& callback) {
    m_callback = callback;
}

void ConcreteListener::onEvent() {
    std::cout << "Listener " << m_name << " received the event notification!" << std::endl;
}

void EventManager::createEvent(const std::string& eventName) {
    if (customEvents.find(eventName) == customEvents.end())
        customEvents.emplace(eventName, EventSource{});
}

EventSource* EventManager::getEvent(const std::string& eventName) {
    auto it = customEvents.find(eventName);
    return (it != customEvents.end()) ? &it->second : nullptr;
}

EventManager eventManager;

// For scripts (plugins, game scripts, etc.)
void createEvent(const std::string& eventName) {
    eventManager.createEvent(eventName);
}

void onCustomEvent(const std::string& eventName,
                   const std::function<void()>& callback) {
    ConcreteListener listener(eventName);
    listener.addCallback(callback);
    eventManager.getEvent(eventName)->addListener(listener);
}

void triggerCustomEvent(const std::string& eventName) {
    eventManager.getEvent(eventName)->triggerEvent();
}

void onEntityCreation(const std::string& listenerName,
                      const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onEntityCreation.addListener(listener);
}

void onEntityDestruction(const std::string& listenerName,
                         const std::function<void()>& callback) {
    ConcreteListener listener(listenerName);
    listener.addCallback(callback);
    eventManager.onEntityDestruction.addListener(listener);
}

PYBIND11_EMBEDDED_MODULE(eventsModule, m) {
    m.def("onEntityCreation", &onEntityCreation,
          "Register a callback for entity creation events",
          py::arg("listenerName"),
          py::arg("callback"));

    m.def("onEntityDestruction", &onEntityDestruction,
          "Register a callback for entity destruction events",
          py::arg("listenerName"),
          py::arg("callback"));

    m.def("createEvent", &createEvent,
        "Create a custom event",
        py::arg("eventName"));

    m.def("onCustomEvent", &onCustomEvent,
        "Register a callback for a custom event",
        py::arg("eventName"),
        py::arg("callback"));

    m.def("triggerCustomEvent", &triggerCustomEvent,
        "Trigger a custom event",
        py::arg("eventName"));
}