module;

#include <cstdint>
#include <functional>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.Events;

export class EventBus {
  public:
    template <typename E>
    uint32_t subscribe(std::function<void(const E&)> fn) {
        return subscribeInternal(typeid(E), [fn = std::move(fn)](const void* payload) {
            fn(*static_cast<const E*>(payload));
        });
    }

    void unsubscribe(uint32_t id) {
        const auto owner = m_owners.find(id);
        if (owner == m_owners.end()) return;

        if (const auto it = m_subscribers.find(owner->second); it != m_subscribers.end()) {
            std::erase_if(it->second, [id](const auto& h) { return h.id == id; });
            if (it->second.empty()) {
                m_subscribers.erase(it);
            }
        }
        m_owners.erase(owner);
    }

    template <typename E>
    [[nodiscard]] bool hasSubscribers() const {
        return hasSubscribers(typeid(E));
    }

    [[nodiscard]] bool hasSubscribers(std::type_index type) const {
        return m_subscribers.contains(type);
    }

    template <typename E>
    void emitNow(const E& event) {
        emitNowInternal(typeid(E), &event);
    }

    template <typename E>
    void emit(E event) {
        if (!hasSubscribers(typeid(E))) return;
        m_queue.push_back([this, event = std::move(event)]() {
            emitNowInternal(typeid(E), &event);
        });
    }

    void dispatch() {
        while (!m_queue.empty()) {
            auto batch = std::exchange(m_queue, {});
            for (auto& fn : batch) fn();
        }
    }

    [[nodiscard]] size_t pending() const { return m_queue.size(); }

  private:
    struct Handler {
        uint32_t id;
        std::function<void(const void*)> callback;
    };

    uint32_t subscribeInternal(std::type_index type, std::function<void(const void*)> fn) {
        const uint32_t id = ++m_nextId;
        m_subscribers[type].push_back({id, std::move(fn)});
        m_owners.emplace(id, type);
        return id;
    }

    void emitNowInternal(std::type_index type, const void* event) {
        const auto it = m_subscribers.find(type);
        if (it == m_subscribers.end()) return;

        const auto snapshot = it->second;
        for (const auto& h : snapshot) {
            h.callback(event);
        }
    }

    std::unordered_map<std::type_index, std::vector<Handler>> m_subscribers;
    std::unordered_map<uint32_t, std::type_index> m_owners;
    std::vector<std::function<void()>> m_queue;
    uint32_t m_nextId = 0;
};