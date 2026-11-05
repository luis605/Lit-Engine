module;

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

export module Engine.Rng;

export class Pcg32 {
  public:
    Pcg32() { seed(0x853c49e6748fea9bULL, 0xda3e39cb94b95bdbULL); }
    explicit Pcg32(uint64_t seedValue, uint64_t sequence = 0xda3e39cb94b95bdbULL) { seed(seedValue, sequence); }

    void seed(uint64_t seedValue, uint64_t sequence = 0xda3e39cb94b95bdbULL) {
        m_state = 0;
        m_inc = (sequence << 1u) | 1u;
        next();
        m_state += seedValue;
        next();
    }

    uint32_t next() {
        const uint64_t previousState = m_state;
        m_state = previousState * 6364136223846793005ULL + m_inc;
        const uint32_t xorshifted = static_cast<uint32_t>(((previousState >> 18u) ^ previousState) >> 27u);
        const uint32_t rotation = static_cast<uint32_t>(previousState >> 59u);
        return (xorshifted >> rotation) | (xorshifted << ((~rotation + 1u) & 31u));
    }

    uint32_t below(uint32_t bound) {
        if (bound == 0) return 0;
        const uint32_t threshold = (~bound + 1u) % bound;
        for (;;) {
            const uint32_t candidate = next();
            if (candidate >= threshold) return candidate % bound;
        }
    }

    float uniform() { return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f); }
    float range(float low, float high) { return low + (high - low) * uniform(); }
    int rangeInt(int low, int high) {
        if (high <= low) return low;
        const uint32_t span = static_cast<uint32_t>(high) - static_cast<uint32_t>(low) + 1u;
        return static_cast<int>(static_cast<uint32_t>(low) + below(span));
    }
    bool chance(float probability) { return uniform() < probability; }

    [[nodiscard]] uint64_t state() const { return m_state; }
    [[nodiscard]] uint64_t increment() const { return m_inc; }
    void restore(uint64_t savedState, uint64_t increment) {
        m_state = savedState;
        m_inc = increment | 1u;
    }
    bool operator==(const Pcg32&) const = default;

  private:
    uint64_t m_state = 0;
    uint64_t m_inc = 1;
};

export constexpr uint64_t hashName(std::string_view text) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char character : text) {
        hash ^= static_cast<unsigned char>(character);
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

export class RandomService {
  public:
    void seed(uint64_t value) {
        m_seed = value;
        for (auto& [name, stream] : m_streams) stream.seed(value, hashName(name));
    }
    [[nodiscard]] uint64_t seed() const { return m_seed; }

    Pcg32& stream(const std::string& name) {
        const auto found = m_streams.find(name);
        if (found != m_streams.end()) return found->second;
        return m_streams.emplace(name, Pcg32(m_seed, hashName(name))).first->second;
    }
    Pcg32& main() { return stream("main"); }
    [[nodiscard]] const std::map<std::string, Pcg32>& streams() const { return m_streams; }
    void clear() { m_streams.clear(); }

  private:
    uint64_t m_seed = 1;
    std::map<std::string, Pcg32> m_streams;
};
