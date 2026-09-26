module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include <algorithm>
#include <cstdint>
#include <charconv>
#include <cstring>
#include <ostream>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

module Engine.Render.custom;

import Engine.glm;

namespace {

using Diligent::RefCntAutoPtr;

struct ParamBlock {
    glm::vec4 params[4];
    glm::vec4 time;
};

struct BindingCache {
    std::vector<std::pair<uint64_t, RefCntAutoPtr<Diligent::IShaderResourceBinding>>> entries;

    Diligent::IShaderResourceBinding* get(Diligent::IPipelineState* pso, const std::vector<litgpu::Named>& resources) {
        uint64_t hash = reinterpret_cast<uint64_t>(pso);
        for (const litgpu::Named& r : resources) hash = (hash ^ reinterpret_cast<uint64_t>(r.object)) * 1099511628211ull;
        for (auto& [key, srb] : entries) {
            if (key == hash) return srb;
        }
        if (entries.size() >= 12) entries.erase(entries.begin());
        RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
        pso->CreateShaderResourceBinding(&srb, true);
        litgpu::bindList(srb, resources);
        entries.emplace_back(hash, srb);
        return entries.back().second;
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
    bool mesh = false;
};

struct UserBuffer {
    RefCntAutoPtr<Diligent::IBuffer> buffer;
    uint32_t stride = 0;
    uint32_t count = 0;
};

}  // namespace

struct CustomPasses::Data {
    Diligent::IRenderDevice* device = nullptr;
    Diligent::IDeviceContext* context = nullptr;
    std::vector<Pass> passes;
    std::unordered_map<std::string, UserBuffer> buffers;
    bool meshShaders = false;
};

CustomPasses::CustomPasses() = default;
CustomPasses::~CustomPasses() { shutdown(); }

void CustomPasses::init(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context) {
    if (!m_data) m_data = new Data();
    m_data->device = device;
    m_data->context = context;
    m_data->meshShaders = device->GetDeviceInfo().Features.MeshShaders == Diligent::DEVICE_FEATURE_STATE_ENABLED;
    Lit::Log::Info("Mesh shaders {}", m_data->meshShaders ? "available" : "not supported by this GPU/driver");
}

void CustomPasses::shutdown() {
    delete m_data;
    m_data = nullptr;
}

bool CustomPasses::meshShadersSupported() const { return m_data && m_data->meshShaders; }

bool CustomPasses::hasEffects() const {
    if (!m_data) return false;
    return std::any_of(m_data->passes.begin(), m_data->passes.end(), [](const Pass& p) { return p.info.kind == PassKind::Effect && p.info.enabled && p.info.valid; });
}

namespace {

std::vector<litgpu::NamedSampler> customSamplers() {
    return {{"u_depth", litgpu::pointClamp()}, {"u_gbuffer0", litgpu::pointClamp()}, {"u_gbuffer1", litgpu::pointClamp()}, {"u_color", litgpu::linearClamp()}};
}

RefCntAutoPtr<Diligent::IPipelineState> buildPass(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders) {
    const auto samplers = customSamplers();
    litgpu::PipelineLayout layout;
    if (pass.info.kind == PassKind::Compute) {
        auto cs = litgpu::compileFile(device, pass.compute.path, Diligent::SHADER_TYPE_COMPUTE);
        if (!cs) return {};
        layout = litgpu::makeLayout({cs});
        for (const auto& s : samplers) layout.samplers.push_back({Diligent::SHADER_TYPE_COMPUTE, s.name, s.desc});
        Diligent::ComputePipelineStateCreateInfo ci;
        ci.PSODesc.Name = pass.info.name.c_str();
        ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_COMPUTE;
        ci.pCS = cs;
        layout.apply(ci.PSODesc.ResourceLayout);
        RefCntAutoPtr<Diligent::IPipelineState> pso;
        device->CreateComputePipelineState(ci, &pso);
        return pso;
    }
    Diligent::GraphicsPipelineStateCreateInfo ci;
    ci.PSODesc.Name = pass.info.name.c_str();
    ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    auto& gp = ci.GraphicsPipeline;
    gp.NumRenderTargets = 1;
    gp.RTVFormats[0] = Diligent::TEX_FORMAT_RGBA16_FLOAT;
    gp.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    RefCntAutoPtr<Diligent::IShader> vs, ps, ms, as;
    if (pass.info.kind == PassKind::Effect) {
        const std::string templ = litgpu::readText("resources/shaders/effects/post_template.frag");
        const std::string user = litgpu::readText(pass.effect.path);
        if (templ.empty() || user.empty()) {
            Lit::Log::Error("Post effect '{}' source missing ({})", pass.info.name, pass.effect.path);
            return {};
        }
        const std::string marker = "#pragma lit_user_code";
        const size_t at = templ.find(marker);
        const std::string source = templ.substr(0, at) + user + "\n" + templ.substr(at + marker.size());
        vs = litgpu::compileFile(device, "resources/shaders/fullscreen.vert", Diligent::SHADER_TYPE_VERTEX);
        ps = litgpu::compileSource(device, pass.effect.path, source, Diligent::SHADER_TYPE_PIXEL);
        if (!vs || !ps) return {};
        gp.DSVFormat = Diligent::TEX_FORMAT_UNKNOWN;
        gp.DepthStencilDesc.DepthEnable = false;
        gp.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        layout = litgpu::makeLayout({vs, ps});
    } else {
        const DrawDesc& d = pass.draw;
        gp.DSVFormat = Diligent::TEX_FORMAT_D32_FLOAT;
        gp.DepthStencilDesc.DepthEnable = d.depthTest;
        gp.DepthStencilDesc.DepthWriteEnable = d.depthWrite;
        gp.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
        gp.RasterizerDesc.CullMode = d.doubleSided ? Diligent::CULL_MODE_NONE : Diligent::CULL_MODE_BACK;
        gp.RasterizerDesc.FrontCounterClockwise = true;
        auto& blend = gp.BlendDesc.RenderTargets[0];
        if (d.blend == DrawBlend::Additive) {
            blend.BlendEnable = true;
            blend.SrcBlend = blend.DestBlend = blend.SrcBlendAlpha = blend.DestBlendAlpha = Diligent::BLEND_FACTOR_ONE;
        } else if (d.blend == DrawBlend::Alpha) {
            blend.BlendEnable = true;
            blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
            blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
            blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
            blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
        }
        ps = litgpu::compileFile(device, d.fragmentPath, Diligent::SHADER_TYPE_PIXEL);
        if (!ps) return {};
        if (pass.mesh) {
            if (!meshShaders) {
                Lit::Log::Warn("Draw '{}' needs mesh shaders, which this GPU does not support; it is skipped", pass.info.name);
                return {};
            }
            ci.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_MESH;
            ms = litgpu::compileFile(device, d.meshPath, Diligent::SHADER_TYPE_MESH);
            if (!ms) return {};
            if (!d.taskPath.empty()) {
                as = litgpu::compileFile(device, d.taskPath, Diligent::SHADER_TYPE_AMPLIFICATION);
                if (!as) return {};
            }
            gp.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_UNDEFINED;
            layout = as ? litgpu::makeLayout({as, ms, ps}) : litgpu::makeLayout({ms, ps});
        } else {
            vs = litgpu::compileFile(device, d.vertexPath, Diligent::SHADER_TYPE_VERTEX);
            if (!vs) return {};
            const Diligent::PRIMITIVE_TOPOLOGY topologies[] = {Diligent::PRIMITIVE_TOPOLOGY_POINT_LIST, Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST, Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
            gp.PrimitiveTopology = topologies[static_cast<int>(d.primitive)];
            layout = litgpu::makeLayout({vs, ps});
        }
    }
    for (const auto& s : samplers) layout.samplers.push_back({Diligent::SHADER_TYPE_ALL_GRAPHICS | Diligent::SHADER_TYPE_ALL_MESH, s.name, s.desc});
    ci.pVS = vs;
    ci.pPS = ps;
    ci.pMS = ms;
    ci.pAS = as;
    layout.apply(ci.PSODesc.ResourceLayout);
    RefCntAutoPtr<Diligent::IPipelineState> pso;
    device->CreateGraphicsPipelineState(ci, &pso);
    return pso;
}

}  // namespace

uint32_t CustomPasses::addCompute(const ComputeDesc& desc) {
    for (Pass& existing : m_data->passes) {
        if (existing.info.kind == PassKind::Compute && existing.info.name == desc.name) {
            if (existing.compute == desc && existing.pso) return existing.info.id;
            existing.compute = desc;
            existing.pso = buildPass(m_data->device, existing, m_data->meshShaders);
            existing.info.valid = existing.pso != nullptr;
            existing.bindings.entries.clear();
            return existing.info.id;
        }
    }
    Pass pass;
    pass.info.kind = PassKind::Compute;
    pass.info.name = desc.name;
    pass.compute = desc;
    pass.info.id = static_cast<uint32_t>(m_data->passes.size());
    pass.params = litgpu::uniformBuffer(m_data->device, "LitParams", sizeof(ParamBlock));
    pass.pso = buildPass(m_data->device, pass, m_data->meshShaders);
    pass.info.valid = pass.pso != nullptr;
    if (!pass.info.valid) Lit::Log::Error("Compute job '{}' failed to build from {}", desc.name, desc.path);
    m_data->passes.push_back(std::move(pass));
    return m_data->passes.back().info.id;
}

uint32_t CustomPasses::addDraw(const DrawDesc& desc) {
    for (Pass& existing : m_data->passes) {
        if (existing.info.kind == PassKind::Draw && existing.info.name == desc.name) {
            if (existing.draw == desc && existing.pso) return existing.info.id;
            existing.draw = desc;
            existing.mesh = !desc.meshPath.empty();
            existing.pso = buildPass(m_data->device, existing, m_data->meshShaders);
            existing.info.valid = existing.pso != nullptr;
            existing.bindings.entries.clear();
            return existing.info.id;
        }
    }
    Pass pass;
    pass.info.kind = PassKind::Draw;
    pass.info.name = desc.name;
    pass.draw = desc;
    pass.mesh = !desc.meshPath.empty();
    pass.info.id = static_cast<uint32_t>(m_data->passes.size());
    pass.params = litgpu::uniformBuffer(m_data->device, "LitParams", sizeof(ParamBlock));
    pass.pso = buildPass(m_data->device, pass, m_data->meshShaders);
    pass.info.valid = pass.pso != nullptr;
    if (!pass.info.valid && (!pass.mesh || m_data->meshShaders)) Lit::Log::Error("Draw '{}' failed to build", desc.name);
    m_data->passes.push_back(std::move(pass));
    return m_data->passes.back().info.id;
}

uint32_t CustomPasses::addEffect(const PostEffectDesc& desc) {
    for (Pass& existing : m_data->passes) {
        if (existing.info.kind == PassKind::Effect && existing.info.name == desc.name) return existing.info.id;
    }
    Pass pass;
    pass.info.kind = PassKind::Effect;
    pass.info.name = desc.name;
    pass.effect = desc;
    pass.info.id = static_cast<uint32_t>(m_data->passes.size());
    pass.params = litgpu::uniformBuffer(m_data->device, "LitParams", sizeof(ParamBlock));
    pass.pso = buildPass(m_data->device, pass, m_data->meshShaders);
    pass.info.valid = pass.pso != nullptr;
    if (!pass.info.valid) Lit::Log::Error("Post effect '{}' failed to build from {}", desc.name, desc.path);
    m_data->passes.push_back(std::move(pass));
    return m_data->passes.back().info.id;
}

void CustomPasses::clearBindings() {
    if (!m_data) return;
    for (Pass& pass : m_data->passes) pass.bindings.entries.clear();
}

void CustomPasses::reload() {
    if (!m_data) return;
    m_data->context->WaitForIdle();
    for (Pass& pass : m_data->passes) {
        auto pso = buildPass(m_data->device, pass, m_data->meshShaders);
        if (pso) {
            pass.pso = pso;
            pass.info.valid = true;
            pass.bindings.entries.clear();
        }
    }
}

void CustomPasses::setBuffer(const std::string& name, uint32_t stride, uint32_t count, const void* data) {
    UserBuffer& ub = m_data->buffers[name];
    if (!ub.buffer || ub.stride != stride || ub.count != count) {
        ub.buffer = litgpu::structuredBuffer(m_data->device, name.c_str(), stride, count, data);
        ub.stride = stride;
        ub.count = count;
        return;
    }
    if (data) m_data->context->UpdateBuffer(ub.buffer, 0, static_cast<Diligent::Uint64>(stride) * count, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

std::vector<uint8_t> CustomPasses::readBuffer(const std::string& name) {
    std::vector<uint8_t> out;
    const auto it = m_data->buffers.find(name);
    if (it == m_data->buffers.end()) return out;
    const Diligent::Uint64 size = static_cast<Diligent::Uint64>(it->second.stride) * it->second.count;
    Diligent::BufferDesc desc;
    desc.Name = "Custom Readback";
    desc.Usage = Diligent::USAGE_STAGING;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    desc.Size = size;
    RefCntAutoPtr<Diligent::IBuffer> staging;
    m_data->device->CreateBuffer(desc, nullptr, &staging);
    m_data->context->CopyBuffer(it->second.buffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, size, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_data->context->WaitForIdle();
    Diligent::MapHelper<uint8_t> mapped(m_data->context, staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
    out.assign(static_cast<const uint8_t*>(mapped), static_cast<const uint8_t*>(mapped) + size);
    return out;
}

void CustomPasses::setParam(PassKind, uint32_t id, int index, const glm::vec4& value) {
    if (!m_data || id >= m_data->passes.size() || index < 0 || index > 3) return;
    m_data->passes[id].info.params[index] = value;
}

void CustomPasses::setEnabled(PassKind, uint32_t id, bool enabled) {
    if (m_data && id < m_data->passes.size()) m_data->passes[id].info.enabled = enabled;
}

void CustomPasses::setComputeGroups(uint32_t id, const glm::ivec3& groups) {
    if (m_data && id < m_data->passes.size()) m_data->passes[id].compute.groups = groups;
}

void CustomPasses::setDrawCounts(uint32_t id, uint32_t vertexCount, uint32_t instanceCount) {
    if (!m_data || id >= m_data->passes.size()) return;
    m_data->passes[id].draw.vertexCount = vertexCount;
    m_data->passes[id].draw.instanceCount = instanceCount;
}

std::vector<PassInfo> CustomPasses::passes() const {
    std::vector<PassInfo> out;
    if (!m_data) return out;
    for (const Pass& p : m_data->passes) out.push_back(p.info);
    return out;
}

namespace {

const char* kindName(PassKind kind) { return kind == PassKind::Compute ? "compute" : kind == PassKind::Draw ? "draw" : "effect"; }

std::vector<std::string_view> splitTabs(std::string_view text) {
    std::vector<std::string_view> parts;
    size_t start = 0;
    while (true) {
        const size_t tab = text.find('\t', start);
        parts.push_back(text.substr(start, tab == std::string_view::npos ? std::string_view::npos : tab - start));
        if (tab == std::string_view::npos) break;
        start = tab + 1;
    }
    return parts;
}

std::vector<float> parseNumbers(std::string_view text) {
    std::vector<float> values;
    while (!text.empty()) {
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        if (text.empty()) break;
        float value = 0.0f;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{}) break;
        values.push_back(value);
        text.remove_prefix(static_cast<size_t>(end - text.data()));
    }
    return values;
}

}  // namespace

void CustomPasses::save(std::ostream& out, std::string_view prefix) const {
    if (!m_data) return;
    for (const Pass& pass : m_data->passes) {
        out << prefix << kindName(pass.info.kind) << ' ' << (pass.info.enabled ? 1 : 0);
        for (const glm::vec4& p : pass.info.params) out << ' ' << p.x << ' ' << p.y << ' ' << p.z << ' ' << p.w;
        if (pass.info.kind == PassKind::Compute) {
            const ComputeDesc& c = pass.compute;
            out << ' ' << c.groups.x << ' ' << c.groups.y << ' ' << c.groups.z << ' ' << static_cast<int>(c.stage) << '\t' << c.name << '\t' << c.path;
        } else if (pass.info.kind == PassKind::Draw) {
            const DrawDesc& dd = pass.draw;
            out << ' ' << static_cast<int>(dd.primitive) << ' ' << dd.vertexCount << ' ' << dd.instanceCount << ' ' << dd.meshGroups.x << ' ' << dd.meshGroups.y << ' ' << dd.meshGroups.z << ' ' << static_cast<int>(dd.blend) << ' '
                << (dd.depthTest ? 1 : 0) << ' ' << (dd.depthWrite ? 1 : 0) << ' ' << (dd.doubleSided ? 1 : 0) << '\t' << dd.name << '\t' << dd.vertexPath << '\t' << dd.fragmentPath << '\t' << dd.meshPath << '\t' << dd.taskPath;
        } else {
            out << '\t' << pass.effect.name << '\t' << pass.effect.path;
        }
        out << '\n';
    }
}

bool CustomPasses::load(std::string_view line) {
    if (!m_data) return false;
    const auto parts = splitTabs(line);
    const size_t space = parts[0].find(' ');
    if (space == std::string_view::npos || parts.size() < 3) return false;
    const std::string_view kind = parts[0].substr(0, space);
    const std::vector<float> n = parseNumbers(parts[0].substr(space + 1));
    if (n.size() < 17) return false;
    uint32_t id = 0;
    PassKind passKind = PassKind::Effect;
    if (kind == "compute" && n.size() >= 21) {
        ComputeDesc desc;
        desc.name = std::string(parts[1]);
        desc.path = std::string(parts[2]);
        desc.groups = glm::ivec3(static_cast<int>(n[17]), static_cast<int>(n[18]), static_cast<int>(n[19]));
        desc.stage = static_cast<ComputeStage>(std::clamp(static_cast<int>(n[20]), 0, 2));
        passKind = PassKind::Compute;
        id = addCompute(desc);
    } else if (kind == "draw" && n.size() >= 27 && parts.size() >= 6) {
        DrawDesc desc;
        desc.name = std::string(parts[1]);
        desc.vertexPath = std::string(parts[2]);
        desc.fragmentPath = std::string(parts[3]);
        desc.meshPath = std::string(parts[4]);
        desc.taskPath = std::string(parts[5]);
        desc.primitive = static_cast<DrawPrimitive>(std::clamp(static_cast<int>(n[17]), 0, 3));
        desc.vertexCount = static_cast<uint32_t>(n[18]);
        desc.instanceCount = static_cast<uint32_t>(n[19]);
        desc.meshGroups = glm::ivec3(static_cast<int>(n[20]), static_cast<int>(n[21]), static_cast<int>(n[22]));
        desc.blend = static_cast<DrawBlend>(std::clamp(static_cast<int>(n[23]), 0, 2));
        desc.depthTest = n[24] > 0.5f;
        desc.depthWrite = n[25] > 0.5f;
        desc.doubleSided = n[26] > 0.5f;
        passKind = PassKind::Draw;
        id = addDraw(desc);
    } else if (kind == "effect") {
        id = addEffect({std::string(parts[1]), std::string(parts[2])});
    } else {
        return false;
    }
    setEnabled(passKind, id, n[0] > 0.5f);
    for (int i = 0; i < 4; ++i) setParam(passKind, id, i, glm::vec4(n[1 + i * 4], n[2 + i * 4], n[3 + i * 4], n[4 + i * 4]));
    return true;
}

namespace {

std::vector<litgpu::Named> gatherResources(const CustomFrame& frame, const std::unordered_map<std::string, UserBuffer>& buffers, Diligent::IBuffer* params) {
    std::vector<litgpu::Named> list;
    list.reserve(frame.resources.size() + buffers.size() + 1);
    for (const auto& [name, ub] : buffers) list.push_back({name, ub.buffer});
    for (const NamedResource& r : frame.resources) list.push_back({r.name, r.object});
    list.push_back({"LitParams", params});
    return list;
}

void updateParams(Diligent::IDeviceContext* context, Pass& pass, const CustomFrame& frame) {
    ParamBlock block{};
    for (int i = 0; i < 4; ++i) block.params[i] = pass.info.params[i];
    block.time = glm::vec4(frame.time, frame.deltaTime, static_cast<float>(frame.frame), 0.0f);
    context->UpdateBuffer(pass.params, 0, sizeof(block), &block, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

}  // namespace

void CustomPasses::runCompute(ComputeStage stage, const CustomFrame& frame) {
    if (!m_data) return;
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Compute || !pass.info.enabled || !pass.pso || pass.compute.stage != stage) continue;
        updateParams(m_data->context, pass, frame);
        const auto resources = gatherResources(frame, m_data->buffers, pass.params);
        Diligent::IShaderResourceBinding* srb = pass.bindings.get(pass.pso, resources);
        const glm::ivec3 g = glm::max(pass.compute.groups, glm::ivec3(1));
        litgpu::dispatch(m_data->context, pass.pso, srb, static_cast<Diligent::Uint32>(g.x), static_cast<Diligent::Uint32>(g.y), static_cast<Diligent::Uint32>(g.z));
    }
}

void CustomPasses::runDraws(const CustomFrame& frame, Diligent::ITextureView* colorTarget, Diligent::ITextureView* depthTarget) {
    if (!m_data) return;
    bool bound = false;
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Draw || !pass.info.enabled || !pass.pso) continue;
        updateParams(m_data->context, pass, frame);
        const auto resources = gatherResources(frame, m_data->buffers, pass.params);
        Diligent::IShaderResourceBinding* srb = pass.bindings.get(pass.pso, resources);
        if (!bound) {
            m_data->context->SetRenderTargets(1, &colorTarget, depthTarget, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Diligent::Viewport viewport;
            viewport.Width = static_cast<float>(frame.width);
            viewport.Height = static_cast<float>(frame.height);
            m_data->context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(frame.width), static_cast<Diligent::Uint32>(frame.height));
            bound = true;
        }
        m_data->context->SetPipelineState(pass.pso);
        m_data->context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (pass.mesh) {
            Diligent::DrawMeshAttribs attribs;
            attribs.ThreadGroupCountX = static_cast<Diligent::Uint32>(std::max(pass.draw.meshGroups.x, 1));
            attribs.ThreadGroupCountY = static_cast<Diligent::Uint32>(std::max(pass.draw.meshGroups.y, 1));
            attribs.ThreadGroupCountZ = static_cast<Diligent::Uint32>(std::max(pass.draw.meshGroups.z, 1));
            m_data->context->DrawMesh(attribs);
        } else {
            Diligent::DrawAttribs attribs;
            attribs.NumVertices = pass.draw.vertexCount;
            attribs.NumInstances = std::max(pass.draw.instanceCount, 1u);
            m_data->context->Draw(attribs);
        }
    }
}

int CustomPasses::runEffects(const CustomFrame& frame, Diligent::ITexture* const targets[2], int current) {
    if (!m_data) return current;
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Effect || !pass.info.enabled || !pass.pso) continue;
        updateParams(m_data->context, pass, frame);
        auto resources = gatherResources(frame, m_data->buffers, pass.params);
        resources.push_back({"u_color", litgpu::srv(targets[current])});
        Diligent::IShaderResourceBinding* srb = pass.bindings.get(pass.pso, resources);
        Diligent::ITextureView* rtv = litgpu::rtv(targets[1 - current]);
        m_data->context->SetRenderTargets(1, &rtv, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::Viewport viewport;
        viewport.Width = static_cast<float>(frame.width);
        viewport.Height = static_cast<float>(frame.height);
        m_data->context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(frame.width), static_cast<Diligent::Uint32>(frame.height));
        m_data->context->SetPipelineState(pass.pso);
        m_data->context->CommitShaderResources(srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_data->context->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
        current = 1 - current;
    }
    return current;
}
