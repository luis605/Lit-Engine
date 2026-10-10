module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/TextureView.h"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

module Engine.Render.observation;

import Engine.glm;

struct ObservationPass::Impl {
    struct Constants {
        glm::mat4 invViewProjection;
        glm::vec4 origin;
        uint32_t dims[4];
    };
    struct Slot {
        Diligent::RefCntAutoPtr<Diligent::IBuffer> staging;
        uint32_t cells = 0;
        uint64_t frame = 0;
        double time = 0.0;
        uint32_t width = 0;
        uint32_t height = 0;
        bool pending = false;
    };

    Diligent::IRenderDevice* device = nullptr;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> spheres;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> ids;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> grid;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants;
    uint32_t gridCapacity = 0;
    Slot slots[ObservationPass::kSlots];
    Diligent::ITexture* boundDepth = nullptr;

    void ensureGrid(uint32_t cells) {
        if (cells <= gridCapacity) return;
        gridCapacity = cells;
        grid = litgpu::structuredBuffer(device, "Observation Grid", sizeof(uint32_t), cells);
        for (Slot& s : slots) {
            Diligent::BufferDesc desc;
            desc.Name = "Observation Staging";
            desc.Usage = Diligent::USAGE_STAGING;
            desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
            desc.Size = static_cast<Diligent::Uint64>(cells) * sizeof(uint32_t);
            s.staging.Release();
            device->CreateBuffer(desc, nullptr, &s.staging);
            s.pending = false;
        }
        srb.Release();
        boundDepth = nullptr;
    }
};

ObservationPass::ObservationPass() : m_impl(std::make_unique<Impl>()) {}
ObservationPass::~ObservationPass() = default;

bool ObservationPass::init(Diligent::IRenderDevice* device) {
    m_impl->device = device;
    m_impl->pso = litgpu::computePipeline(device, "resources/shaders/observation_ids.comp", {litgpu::NamedSampler{"u_depth", litgpu::pointClamp()}}, {}, {"u_depth"});
    m_impl->spheres = litgpu::structuredBuffer(device, "Observation Spheres", sizeof(glm::vec4), kMaxSpheres);
    m_impl->ids = litgpu::structuredBuffer(device, "Observation Entity Ids", sizeof(uint32_t), kMaxSpheres);
    m_impl->constants = litgpu::uniformBuffer(device, "Observation Constants", sizeof(Impl::Constants));
    return ready();
}

bool ObservationPass::ready() const { return m_impl->pso && m_impl->spheres && m_impl->ids && m_impl->constants; }

bool ObservationPass::poll(Diligent::IDeviceContext* context, uint32_t slot, ObservationReadback& out) {
    Impl::Slot& s = m_impl->slots[slot % kSlots];
    if (!s.pending || !s.staging) return false;
    s.pending = false;
    Diligent::MapHelper<uint32_t> mapped(context, s.staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
    const uint32_t* data = mapped;
    if (!data) return false;
    out.frame = s.frame;
    out.time = s.time;
    out.width = s.width;
    out.height = s.height;
    out.ids.assign(data, data + s.cells);
    return true;
}

void ObservationPass::record(Diligent::IDeviceContext* context, Diligent::ITexture* depth, uint32_t slot, const ObservationQuery& query) {
    Impl& m = *m_impl;
    if (!ready() || !depth || query.width == 0 || query.height == 0) return;
    const uint32_t cells = query.width * query.height;
    m.ensureGrid(cells);
    if (!m.srb) {
        m.srb = litgpu::makeBinding(m.pso, {{"ObservationSpheres", m.spheres.RawPtr()}, {"ObservationEntityIds", m.ids.RawPtr()}, {"ObservationGrid", m.grid.RawPtr()}, {"ObservationConstants", m.constants.RawPtr()}});
    }
    if (!m.srb) return;
    litgpu::setVar(m.srb, "u_depth", litgpu::srv(depth));
    const uint32_t count = static_cast<uint32_t>(std::min<size_t>(std::min(query.spheres.size(), query.ids.size()), kMaxSpheres));
    if (count > 0) {
        context->UpdateBuffer(m.spheres, 0, count * sizeof(glm::vec4), query.spheres.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->UpdateBuffer(m.ids, 0, count * sizeof(uint32_t), query.ids.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    Impl::Constants c{};
    c.invViewProjection = query.invViewProjection;
    c.origin = glm::vec4(query.origin, 1.0f);
    c.dims[0] = query.width;
    c.dims[1] = query.height;
    c.dims[2] = count;
    context->UpdateBuffer(m.constants, 0, sizeof(c), &c, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    litgpu::dispatch(context, m.pso, m.srb, (query.width + 7) / 8, (query.height + 7) / 8);
    Impl::Slot& s = m.slots[slot % kSlots];
    if (!s.staging) return;
    context->CopyBuffer(m.grid, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, s.staging, 0, cells * sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    s.cells = cells;
    s.frame = query.frame;
    s.time = query.time;
    s.width = query.width;
    s.height = query.height;
    s.pending = true;
}
