#include "Events.h"

void EventSource::addListener(ConcreteListener& listener) {
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

EventManager eventManager;