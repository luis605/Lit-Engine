module;

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

export module Engine.Params;

export inline constexpr uint32_t kParamSlots = 256;

export enum class CurveMode { Linear, Smooth, Step };

export struct ParamCurve {
    std::vector<std::pair<float, float>> points;
    CurveMode mode = CurveMode::Linear;
    [[nodiscard]] float eval(float x) const;
};

export struct ParamInfo {
    std::string name;
    uint32_t slot = 0;
    float target = 0.0f;
    float value = 0.0f;
    float minimum = -std::numeric_limits<float>::infinity();
    float maximum = std::numeric_limits<float>::infinity();
    float smooth = 0.0f;
    bool derived = false;
    std::string input;
};

struct ParamDefinitions {
    struct Param {
        std::string name;
        float initial = 0.0f;
        float smooth = 0.0f;
        float minimum = -std::numeric_limits<float>::infinity();
        float maximum = std::numeric_limits<float>::infinity();
        int slot = -1;
    };
    struct Mapping {
        std::string name;
        std::string input;
        ParamCurve curve;
        float smooth = 0.0f;
        float minimum = -std::numeric_limits<float>::infinity();
        float maximum = std::numeric_limits<float>::infinity();
        int slot = -1;
    };
    std::vector<Param> params;
    std::vector<Mapping> mappings;
};

export class ParamBus {
  public:
    ParamBus();

    uint32_t declare(const std::string& name, float initial = 0.0f, float smoothSeconds = 0.0f, float minimum = -std::numeric_limits<float>::infinity(), float maximum = std::numeric_limits<float>::infinity(), int slot = -1);
    void set(const std::string& name, float value);
    void snap(const std::string& name, float value);
    [[nodiscard]] float get(const std::string& name) const;
    [[nodiscard]] float target(const std::string& name) const;
    [[nodiscard]] bool has(const std::string& name) const { return m_index.contains(name); }
    [[nodiscard]] int slot(const std::string& name) const;
    [[nodiscard]] float valueAt(uint32_t slot) const { return slot < kParamSlots ? m_values[slot] : 0.0f; }
    [[nodiscard]] const float* values() const { return m_values.data(); }
    [[nodiscard]] const std::vector<ParamInfo>& params() const { return m_params; }
    [[nodiscard]] const ParamCurve* curve(const std::string& name) const;
    [[nodiscard]] uint64_t version() const { return m_version; }

    void update(float deltaSeconds);
    void snapAll();

    bool loadText(std::string_view text, std::string* error = nullptr);
    bool loadFile(const std::string& path, std::string* error = nullptr);
    bool pollReload(std::string* error = nullptr);
    [[nodiscard]] const std::string& watchedPath() const { return m_path; }
    [[nodiscard]] uint32_t reloadCount() const { return m_reloads; }

  private:
    struct Mapping {
        uint32_t param = 0;
        uint32_t input = 0;
        ParamCurve curve;
    };

    uint32_t ensure(const std::string& name);
    uint32_t addParam(const std::string& name, uint32_t slot, uint8_t fromFile);
    uint32_t place(const std::string& name, int slot);
    [[nodiscard]] int freeSlot() const;
    void rebuildIndex();
    void stepSmoothing(ParamInfo& info, float deltaSeconds);
    void applyDefinitions(const ParamDefinitions& definitions);
    void applyParamDefinition(const ParamDefinitions::Param& definition, const std::unordered_map<std::string, std::pair<float, float>>& saved);
    void applyMappingDefinition(const ParamDefinitions::Mapping& definition, const std::unordered_map<std::string, std::pair<float, float>>& saved);

    std::vector<ParamInfo> m_params;
    std::vector<Mapping> m_mappings;
    std::vector<uint8_t> m_fromFile;
    std::unordered_map<std::string, uint32_t> m_index;
    std::vector<float> m_values;
    std::vector<uint8_t> m_slotUsed;
    uint64_t m_version = 0;
    std::string m_path;
    std::filesystem::file_time_type m_stamp{};
    bool m_stampValid = false;
    uint32_t m_reloads = 0;
};
