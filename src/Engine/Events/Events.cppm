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

class IEventChannel {
  public:
    virtual ~IEventChannel() = default;
    virtual bool empty() const = 0;
    virtual void remove(uint32_t id) = 0;
};

template <typename E>
class EventChannel final : public IEventChannel {
  public:
    uint32_t add(std::function<void(const E&)> fn, uint32_t id) {
        m_handlers.emplace_back(id, std::move(fn));
        return id;
    }
    void remove(uint32_t id) override {
        std::erase_if(m_handlers, [id](const auto& h) { return h.first == id; });
    }
    bool empty() const override { return m_handlers.empty(); }
    void invoke(const E& e) {
        const auto snapshot = m_handlers;
        for (const auto& [id, fn] : snapshot) fn(e);
    }

  private:
    std::vector<std::pair<uint32_t, std::function<void(const E&)>>> m_handlers;
};

export class EventBus {
  public:
    template <typename E>
    uint32_t subscribe(std::function<void(const E&)> fn) {
        const uint32_t id = ++m_nextId;
        channel<E>().add(std::move(fn), id);
        m_owners.insert_or_assign(id, std::type_index(typeid(E)));
        ++m_subscriberCount;
        return id;
    }

    void unsubscribe(uint32_t id) {
        const auto owner = m_owners.find(id);
        if (owner == m_owners.end()) return;
        const auto it = m_channels.find(owner->second);
        if (it != m_channels.end()) it->second->remove(id);
        m_owners.erase(owner);
        --m_subscriberCount;
    }

    template <typename E>
    [[nodiscard]] bool hasSubscribers() const {
        if (m_subscriberCount == 0) return false;
        const auto it = m_channels.find(std::type_index(typeid(E)));
        return it != m_channels.end() && !it->second->empty();
    }

    template <typename E>
    void emitNow(const E& event) {
        if (!hasSubscribers<E>()) return;
        channel<E>().invoke(event);
    }

    template <typename E>
    void emit(E event) {
        if (!hasSubscribers<E>()) return;
        m_queue.push_back([this, event = std::move(event)]() { emitNow(event); });
    }

    void dispatch() {
        while (!m_queue.empty()) {
            auto batch = std::move(m_queue);
            m_queue.clear();
            for (auto& fn : batch) fn();
        }
    }

    [[nodiscard]] size_t pending() const { return m_queue.size(); }

  private:
    template <typename E>
    EventChannel<E>& channel() {
        auto& slot = m_channels[std::type_index(typeid(E))];
        if (!slot) slot = std::make_unique<EventChannel<E>>();
        return static_cast<EventChannel<E>&>(*slot);
    }

    std::unordered_map<std::type_index, std::unique_ptr<IEventChannel>> m_channels;
    std::unordered_map<uint32_t, std::type_index> m_owners;
    std::vector<std::function<void()>> m_queue;
    uint32_t m_nextId = 0;
    uint32_t m_subscriberCount = 0;
};
