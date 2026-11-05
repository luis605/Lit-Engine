module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Engine.WorldHash;

import Engine.World;
import Engine.State;
import Engine.Storylet;
import Engine.Params;
import Engine.Nav;

export inline constexpr float kHashQuantum = 1.0e-4f;

export class StateHasher {
  public:
    void addBytes(const void* data, size_t size);
    void addU64(uint64_t value);
    void addI64(int64_t value) { addU64(static_cast<uint64_t>(value)); }
    void addBool(bool value) { addU64(value ? 1u : 0u); }
    void addText(std::string_view text);
    void addFloat(float value);
    void addVec3(float x, float y, float z);
    [[nodiscard]] uint64_t value() const { return m_state; }

  private:
    uint64_t m_state = 0xcbf29ce484222325ULL;
};

export struct HashSources {
    const StateStore* state = nullptr;
    const StoryletEngine* storylets = nullptr;
    const ParamBus* params = nullptr;
    const NavMesh* nav = nullptr;
    std::vector<uint64_t> extra;
};

export [[nodiscard]] int64_t quantiseForHash(float value);
export [[nodiscard]] uint64_t hashWorldState(const World& world, const HashSources& sources = {});
