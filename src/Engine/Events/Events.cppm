module;

#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.Events;

export class EventBus {
  public:
    template <typename Event>
    uint32_t subscribe(std::function<void(const Event&)> handler) {
        return subscribeInternal(typeid(Event), [handler = std::move(handler)](const void* payload) {
            handler(*static_cast<const Event*>(payload));
        });
    }

    void unsubscribe(uint32_t id) {
        const auto owner = m_owners.find(id);
        if (owner == m_owners.end()) return;

        if (const auto found = m_subscribers.find(owner->second); found != m_subscribers.end()) {
            HandlerList& handlers = mutableHandlers(found->second);
            std::erase_if(handlers, [id](const Handler& handler) { return handler.id == id; });
            if (handlers.empty()) {
                m_subscribers.erase(found);
            }
        }
        m_owners.erase(owner);
    }

    template <typename Event>
    [[nodiscard]] bool hasSubscribers() const {
        return hasSubscribers(typeid(Event));
    }

    [[nodiscard]] bool hasSubscribers(std::type_index type) const {
        return m_subscribers.contains(type);
    }

    template <typename Event>
    void emitNow(const Event& event) {
        emitNowInternal(typeid(Event), &event);
    }

    template <typename Event>
    void emit(Event event) {
        if (!hasSubscribers(typeid(Event))) return;
        m_queue.push_back([this, event = std::move(event)]() {
            emitNowInternal(typeid(Event), &event);
        });
    }

    void dispatch() {
        while (!m_queue.empty()) {
            auto batch = std::exchange(m_queue, {});
            for (auto& queued : batch) queued();
        }
    }

    [[nodiscard]] size_t pending() const { return m_queue.size(); }

  private:
    struct Handler {
        uint32_t id;
        std::function<void(const void*)> callback;
    };

    using HandlerList = std::vector<Handler>;

    static HandlerList& mutableHandlers(std::shared_ptr<HandlerList>& handlers) {
        if (!handlers) handlers = std::make_shared<HandlerList>();
        else if (handlers.use_count() > 1) handlers = std::make_shared<HandlerList>(*handlers);
        return *handlers;
    }

    uint32_t subscribeInternal(std::type_index type, std::function<void(const void*)> callback) {
        const uint32_t id = ++m_nextId;
        mutableHandlers(m_subscribers[type]).push_back({id, std::move(callback)});
        m_owners.emplace(id, type);
        return id;
    }

    void emitNowInternal(std::type_index type, const void* event) {
        const auto found = m_subscribers.find(type);
        if (found == m_subscribers.end()) return;

        const std::shared_ptr<const HandlerList> snapshot = found->second;
        for (const Handler& handler : *snapshot) {
            handler.callback(event);
        }
    }

    std::unordered_map<std::type_index, std::shared_ptr<HandlerList>> m_subscribers;
    std::unordered_map<uint32_t, std::type_index> m_owners;
    std::vector<std::function<void()>> m_queue;
    uint32_t m_nextId = 0;
};