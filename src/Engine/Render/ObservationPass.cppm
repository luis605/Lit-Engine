module;

#include <cstdint>
#include <memory>
#include <vector>

namespace Diligent {
struct IRenderDevice;
struct IDeviceContext;
struct ITexture;
}

export module Engine.Render.observation;

import Engine.glm;

export struct ObservationQuery {
    std::vector<glm::vec4> spheres;
    std::vector<uint32_t> ids;
    glm::mat4 invViewProjection{1.0f};
    glm::vec3 origin{0.0f};
    uint32_t width = 96;
    uint32_t height = 54;
    uint64_t frame = 0;
    double time = 0.0;
};

export struct ObservationReadback {
    uint64_t frame = 0;
    double time = 0.0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint32_t> ids;
};

export class ObservationPass {
  public:
    static constexpr uint32_t kSlots = 3;
    static constexpr uint32_t kMaxSpheres = 4096;

    ObservationPass();
    ~ObservationPass();
    ObservationPass(const ObservationPass&) = delete;
    ObservationPass& operator=(const ObservationPass&) = delete;

    bool init(Diligent::IRenderDevice* device);
    [[nodiscard]] bool ready() const;
    bool poll(Diligent::IDeviceContext* context, uint32_t slot, ObservationReadback& out);
    void record(Diligent::IDeviceContext* context, Diligent::ITexture* depth, uint32_t slot, const ObservationQuery& query);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
