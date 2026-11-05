#pragma once

namespace custom_detail {

using Diligent::RefCntAutoPtr;

inline constexpr uint32_t kTimerSlots = 4;
inline constexpr size_t kMaxCachedBindings = 12;
inline constexpr int kParamCount = 4;
inline constexpr Diligent::TEXTURE_FORMAT kLdrFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;

struct ParamBlock {
    glm::vec4 params[kParamCount];
    glm::vec4 time;
};

struct BindingCache {
    struct Entry {
        uint64_t hash = 0;
        RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        bool complete = false;
    };
    std::vector<Entry> entries;

    inline Diligent::IShaderResourceBinding* get(Diligent::IPipelineState* pso, const std::vector<litgpu::Named>& resources) {
        uint64_t hash = reinterpret_cast<uint64_t>(pso);
        for (const litgpu::Named& resource : resources) hash = (hash ^ reinterpret_cast<uint64_t>(resource.object)) * 1099511628211ull;
        for (const Entry& entry : entries) {
            if (entry.hash == hash) return entry.complete ? entry.srb.RawPtr() : nullptr;
        }
        if (entries.size() >= kMaxCachedBindings) entries.erase(entries.begin());
        RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        pso->CreateShaderResourceBinding(&srb, true);
        const bool complete = litgpu::bindList(srb, resources);
        entries.push_back({hash, srb, complete});
        return complete ? srb.RawPtr() : nullptr;
    }
};

struct Pass {
    PassInfo info;
    ComputeDesc compute;
    DrawDesc draw;
    PostEffectDesc effect;
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    RefCntAutoPtr<Diligent::IBuffer> params;
    BindingCache bindings;
    RefCntAutoPtr<Diligent::IQuery> timers[kTimerSlots][2];
    bool timed[kTimerSlots] = {};
    bool mesh = false;
    glm::vec4 paramDefaults[kParamCount]{};
    uint32_t paramDefaultMask = 0;
};

struct UserBuffer {
    RefCntAutoPtr<Diligent::IBuffer> buffer;
    uint32_t stride = 0;
    uint32_t count = 0;
};

class PassTimer {
  public:
    inline PassTimer(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Pass& pass, const CustomFrame& frame) : m_context(context), m_pass(pass), m_slot(frame.frameSlot % kTimerSlots) {
        if (!frame.profile) return;
        if (!pass.timers[m_slot][0]) {
            Diligent::QueryDesc desc;
            desc.Type = Diligent::QUERY_TYPE_TIMESTAMP;
            desc.Name = pass.info.name.c_str();
            for (auto& query : pass.timers[m_slot]) device->CreateQuery(desc, &query);
        }
        if (!pass.timers[m_slot][0] || !pass.timers[m_slot][1]) return;
        m_active = true;
        context->EndQuery(pass.timers[m_slot][0]);
    }
    inline ~PassTimer() {
        if (!m_active) return;
        m_context->EndQuery(m_pass.timers[m_slot][1]);
        m_pass.timed[m_slot] = true;
    }
    PassTimer(const PassTimer&) = delete;
    PassTimer& operator=(const PassTimer&) = delete;

  private:
    Diligent::IDeviceContext* m_context;
    Pass& m_pass;
    uint32_t m_slot;
    bool m_active = false;
};

RefCntAutoPtr<Diligent::IPipelineState> buildPass(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders);

}

struct CustomPasses::Data {
    Diligent::IRenderDevice* device = nullptr;
    Diligent::IDeviceContext* context = nullptr;
    std::vector<custom_detail::Pass> passes;
    std::unordered_map<std::string, custom_detail::UserBuffer> buffers;
    std::unordered_map<std::string, custom_detail::RefCntAutoPtr<Diligent::ITexture>> textures;
    custom_detail::RefCntAutoPtr<Diligent::ITexture> ldrScratch;
    bool meshShaders = false;
};
