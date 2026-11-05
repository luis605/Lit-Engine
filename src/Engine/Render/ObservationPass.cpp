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

namespace {
constexpr uint32_t kThreadGroupSize = 8;
}

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

    void ensureGrid(uint32_t cells) {
        if (cells <= gridCapacity) return;
        gridCapacity = cells;
        grid = litgpu::structuredBuffer(device, "Observation Grid", sizeof(uint32_t), cells);
        for (Slot& slot : slots) {
            Diligent::BufferDesc desc;
            desc.Name = "Observation Staging";
            desc.Usage = Diligent::USAGE_STAGING;
            desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
            desc.Size = static_cast<Diligent::Uint64>(cells) * sizeof(uint32_t);
            slot.staging.Release();
            device->CreateBuffer(desc, nullptr, &slot.staging);
            slot.pending = false;
        }
        srb.Release();
    }

    void uploadQuery(Diligent::IDeviceContext* context, const ObservationQuery& query, uint32_t count) {
        constexpr auto kTransition = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        if (count > 0) {
            context->UpdateBuffer(spheres, 0, count * sizeof(glm::vec4), query.spheres.data(), kTransition);
            context->UpdateBuffer(ids, 0, count * sizeof(uint32_t), query.ids.data(), kTransition);
        }
        Constants uniforms{};
        uniforms.invViewProjection = query.invViewProjection;
        uniforms.origin = glm::vec4(query.origin, 1.0f);
        uniforms.dims[0] = query.width;
        uniforms.dims[1] = query.height;
        uniforms.dims[2] = count;
        context->UpdateBuffer(constants, 0, sizeof(uniforms), &uniforms, kTransition);
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
    Impl::Slot& readSlot = m_impl->slots[slot % kSlots];
    if (!readSlot.pending || !readSlot.staging) return false;
    Diligent::MapHelper<uint32_t> mapped(context, readSlot.staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
    const uint32_t* data = mapped;
    if (!data) return false;
    readSlot.pending = false;
    out.frame = readSlot.frame;
    out.time = readSlot.time;
    out.width = readSlot.width;
    out.height = readSlot.height;
    out.ids.assign(data, data + readSlot.cells);
    return true;
}

void ObservationPass::record(Diligent::IDeviceContext* context, Diligent::ITexture* depth, uint32_t slot, const ObservationQuery& query) {
    Impl& impl = *m_impl;
    if (!ready() || !depth || query.width == 0 || query.height == 0) return;
    const uint32_t cells = query.width * query.height;
    impl.ensureGrid(cells);
    if (!impl.srb) {
        impl.srb = litgpu::makeBinding(impl.pso, {{"ObservationSpheres", impl.spheres.RawPtr()}, {"ObservationEntityIds", impl.ids.RawPtr()}, {"ObservationGrid", impl.grid.RawPtr()}, {"ObservationConstants", impl.constants.RawPtr()}});
    }
    if (!impl.srb) return;
    litgpu::setVar(impl.srb, "u_depth", litgpu::srv(depth));
    const auto count = static_cast<uint32_t>(std::min<size_t>(std::min(query.spheres.size(), query.ids.size()), kMaxSpheres));
    impl.uploadQuery(context, query, count);
    litgpu::dispatch(context, impl.pso, impl.srb, (query.width + kThreadGroupSize - 1) / kThreadGroupSize, (query.height + kThreadGroupSize - 1) / kThreadGroupSize);
    Impl::Slot& writeSlot = impl.slots[slot % kSlots];
    if (!writeSlot.staging) return;
    context->CopyBuffer(impl.grid, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, writeSlot.staging, 0, cells * sizeof(uint32_t), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    writeSlot.cells = cells;
    writeSlot.frame = query.frame;
    writeSlot.time = query.time;
    writeSlot.width = query.width;
    writeSlot.height = query.height;
    writeSlot.pending = true;
}
