module;

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

export module Engine.State;

import Engine.Events;
import Engine.Json;

export enum class VarKind { Range, Flag, Counter, Set };

export struct StateChanged {
    std::string name;
    VarKind kind = VarKind::Range;
    double oldValue = 0.0;
    double newValue = 0.0;
    std::string member;
    bool added = false;
};

export struct StateVar {
    VarKind kind = VarKind::Range;
    double minimum = 0.0;
    double maximum = 1.0;
    double initial = 0.0;
    double value = 0.0;
    std::set<std::string> members;
};

export class StateStore {
  public:
    explicit StateStore(EventBus* bus = nullptr) : m_bus(bus) {}
    void setBus(EventBus* bus) { m_bus = bus; }

    bool loadSchema(std::string_view text, std::string* error = nullptr);
    bool loadSchemaFile(const std::string& path, std::string* error = nullptr);
    bool declare(const std::string& name, VarKind kind, double minimum = 0.0, double maximum = 1.0, double initial = 0.0);
    [[nodiscard]] bool has(const std::string& name) const { return m_vars.contains(name); }
    [[nodiscard]] const StateVar* var(const std::string& name) const;
    [[nodiscard]] const std::map<std::string, StateVar>& vars() const { return m_vars; }

    [[nodiscard]] double get(const std::string& name) const;
    [[nodiscard]] bool flag(const std::string& name) const { return get(name) != 0.0; }
    [[nodiscard]] bool hasMember(const std::string& name, const std::string& member) const;
    [[nodiscard]] std::vector<std::string> members(const std::string& name) const;

    bool set(const std::string& name, double value);
    bool add(const std::string& name, double delta);
    bool setFlag(const std::string& name, bool value) { return set(name, value ? 1.0 : 0.0); }
    bool addMember(const std::string& name, const std::string& member);
    bool removeMember(const std::string& name, const std::string& member);
    void clearSet(const std::string& name);
    void reset();

    [[nodiscard]] Json toJson() const;
    bool fromJson(const Json& doc, std::string* error = nullptr);

  private:
    void notify(const std::string& name, const StateVar& v, double oldValue, const std::string& member, bool added);

    std::map<std::string, StateVar> m_vars;
    EventBus* m_bus = nullptr;
};
