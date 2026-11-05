module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

module Engine.Render.custom;

import Engine.glm;

#include "CustomPassesInternal.hpp"

using namespace custom_detail;

namespace {

constexpr int kChannelsRgba = 4;

Diligent::TEXTURE_FORMAT textureFormatFor(ShaderTextureFormat format) {
    switch (format) {
        case ShaderTextureFormat::RGBA8: return Diligent::TEX_FORMAT_RGBA8_UNORM;
        case ShaderTextureFormat::RGBA16F: return Diligent::TEX_FORMAT_RGBA16_FLOAT;
        case ShaderTextureFormat::RGBA32F: return Diligent::TEX_FORMAT_RGBA32_FLOAT;
        case ShaderTextureFormat::R32F: return Diligent::TEX_FORMAT_R32_FLOAT;
    }
    return Diligent::TEX_FORMAT_RGBA8_UNORM;
}

void attachSampler(Diligent::IRenderDevice* device, Diligent::ITexture* texture, const Diligent::SamplerDesc& samplerDesc) {
    RefCntAutoPtr<Diligent::ISampler> sampler;
    device->CreateSampler(samplerDesc, &sampler);
    texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(sampler);
}

Pass* findPass(std::vector<Pass>& passes, PassKind kind, const std::string& name) {
    const auto found = std::find_if(passes.begin(), passes.end(), [&](const Pass& pass) { return pass.info.kind == kind && pass.info.name == name; });
    return found == passes.end() ? nullptr : &*found;
}

Pass makePass(Diligent::IRenderDevice* device, PassKind kind, const std::string& name, uint32_t id) {
    Pass pass;
    pass.info.kind = kind;
    pass.info.name = name;
    pass.info.id = id;
    pass.params = litgpu::uniformBuffer(device, "LitParams", sizeof(ParamBlock));
    return pass;
}

void rebuild(Diligent::IRenderDevice* device, Pass& pass, bool meshShaders) {
    pass.pso = buildPass(device, pass, meshShaders);
    pass.info.valid = pass.pso != nullptr;
}

}

std::vector<ShaderError> takeShaderErrors() {
    std::vector<ShaderError> errors;
    for (litgpu::ShaderDiagnostic& diagnostic : litgpu::takeShaderDiagnostics()) errors.push_back({std::move(diagnostic.source), std::move(diagnostic.message)});
    return errors;
}

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

bool CustomPasses::hasEffects(EffectStage stage) const {
    if (!m_data) return false;
    return std::any_of(m_data->passes.begin(), m_data->passes.end(), [&](const Pass& pass) { return pass.info.kind == PassKind::Effect && pass.info.enabled && pass.pso && pass.info.stage == stage; });
}

void CustomPasses::resetTimers(uint32_t frameSlot) {
    if (!m_data) return;
    for (Pass& pass : m_data->passes) pass.timed[frameSlot % kTimerSlots] = false;
}

void CustomPasses::readTimings(uint32_t frameSlot) {
    if (!m_data) return;
    const uint32_t slot = frameSlot % kTimerSlots;
    for (Pass& pass : m_data->passes) {
        pass.info.gpuMilliseconds = 0.0;
        if (!pass.timed[slot]) continue;
        Diligent::QueryDataTimestamp start{};
        Diligent::QueryDataTimestamp end{};
        if (!pass.timers[slot][0]->GetData(&start, sizeof(start), false) || !pass.timers[slot][1]->GetData(&end, sizeof(end), false)) continue;
        if (end.Counter > start.Counter && end.Frequency > 0) pass.info.gpuMilliseconds = static_cast<double>(end.Counter - start.Counter) / static_cast<double>(end.Frequency) * 1000.0;
    }
}

uint32_t CustomPasses::addCompute(const ComputeDesc& desc) {
    if (Pass* existing = findPass(m_data->passes, PassKind::Compute, desc.name)) {
        if (existing->compute == desc && existing->pso) return existing->info.id;
        existing->compute = desc;
        rebuild(m_data->device, *existing, m_data->meshShaders);
        existing->bindings.entries.clear();
        return existing->info.id;
    }
    Pass pass = makePass(m_data->device, PassKind::Compute, desc.name, static_cast<uint32_t>(m_data->passes.size()));
    pass.compute = desc;
    rebuild(m_data->device, pass, m_data->meshShaders);
    if (!pass.info.valid) Lit::Log::Error("Compute job '{}' failed to build from {}", desc.name, desc.path);
    m_data->passes.push_back(std::move(pass));
    return m_data->passes.back().info.id;
}

uint32_t CustomPasses::addDraw(const DrawDesc& desc) {
    if (Pass* existing = findPass(m_data->passes, PassKind::Draw, desc.name)) {
        if (existing->draw == desc && existing->pso) return existing->info.id;
        existing->draw = desc;
        existing->mesh = !desc.meshPath.empty();
        rebuild(m_data->device, *existing, m_data->meshShaders);
        existing->bindings.entries.clear();
        return existing->info.id;
    }
    Pass pass = makePass(m_data->device, PassKind::Draw, desc.name, static_cast<uint32_t>(m_data->passes.size()));
    pass.draw = desc;
    pass.mesh = !desc.meshPath.empty();
    rebuild(m_data->device, pass, m_data->meshShaders);
    if (!pass.info.valid && (!pass.mesh || m_data->meshShaders)) Lit::Log::Error("Draw '{}' failed to build", desc.name);
    m_data->passes.push_back(std::move(pass));
    return m_data->passes.back().info.id;
}

uint32_t CustomPasses::addEffect(const PostEffectDesc& desc) {
    if (Pass* existing = findPass(m_data->passes, PassKind::Effect, desc.name)) return existing->info.id;
    Pass pass = makePass(m_data->device, PassKind::Effect, desc.name, static_cast<uint32_t>(m_data->passes.size()));
    pass.effect = desc;
    rebuild(m_data->device, pass, m_data->meshShaders);
    for (int index = 0; index < kParamCount; ++index) {
        if (pass.paramDefaultMask & (1u << index)) pass.info.params[index] = pass.paramDefaults[index];
    }
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
        if (!pso) continue;
        pass.pso = pso;
        pass.info.valid = true;
        pass.bindings.entries.clear();
    }
}

void CustomPasses::setBuffer(const std::string& name, uint32_t stride, uint32_t count, const void* data) {
    UserBuffer& userBuffer = m_data->buffers[name];
    if (!userBuffer.buffer || userBuffer.stride != stride || userBuffer.count != count) {
        userBuffer.buffer = litgpu::structuredBuffer(m_data->device, name.c_str(), stride, count, data, Diligent::BIND_INDIRECT_DRAW_ARGS);
        clearBindings();
        userBuffer.stride = stride;
        userBuffer.count = count;
        return;
    }
    if (data) m_data->context->UpdateBuffer(userBuffer.buffer, 0, static_cast<Diligent::Uint64>(stride) * count, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

bool CustomPasses::createTexture(const std::string& name, int width, int height, ShaderTextureFormat format) {
    if (!m_data || width <= 0 || height <= 0) return false;
    const Diligent::TEXTURE_FORMAT textureFormat = textureFormatFor(format);
    auto& slot = m_data->textures[name];
    if (slot) {
        const auto& desc = slot->GetDesc();
        if (static_cast<int>(desc.Width) == width && static_cast<int>(desc.Height) == height && desc.Format == textureFormat && (desc.BindFlags & Diligent::BIND_UNORDERED_ACCESS)) return true;
    }
    const Diligent::BIND_FLAGS bindFlags = Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_UNORDERED_ACCESS | Diligent::BIND_RENDER_TARGET;
    auto texture = litgpu::texture(m_data->device, litgpu::TextureSpec{name.c_str(), Diligent::RESOURCE_DIM_TEX_2D, static_cast<Diligent::Uint32>(width), static_cast<Diligent::Uint32>(height), 1, 1, textureFormat, bindFlags});
    if (!texture) return false;
    attachSampler(m_data->device, texture, litgpu::linearClamp());
    m_data->context->WaitForIdle();
    slot = texture;
    clearBindings();
    return true;
}

bool CustomPasses::loadTexture(const std::string& name, const std::string& path, bool srgb) {
    if (!m_data) return false;
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, kChannelsRgba);
    if (!pixels) {
        Lit::Log::Warn("Could not load shader texture '{}' from {}", name, path);
        return false;
    }
    Diligent::TextureDesc desc;
    desc.Name = name.c_str();
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(width);
    desc.Height = static_cast<Diligent::Uint32>(height);
    desc.Format = srgb ? Diligent::TEX_FORMAT_RGBA8_UNORM_SRGB : Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    desc.Usage = Diligent::USAGE_IMMUTABLE;
    Diligent::TextureSubResData level{pixels, static_cast<Diligent::Uint64>(width) * kChannelsRgba};
    Diligent::TextureData data{&level, 1};
    RefCntAutoPtr<Diligent::ITexture> texture;
    m_data->device->CreateTexture(desc, &data, &texture);
    stbi_image_free(pixels);
    if (!texture) return false;
    attachSampler(m_data->device, texture, litgpu::linearWrap());
    m_data->context->WaitForIdle();
    m_data->textures[name] = texture;
    clearBindings();
    return true;
}

std::vector<uint8_t> CustomPasses::readBuffer(const std::string& name) {
    std::vector<uint8_t> out;
    const auto found = m_data->buffers.find(name);
    if (found == m_data->buffers.end()) return out;
    const Diligent::Uint64 size = static_cast<Diligent::Uint64>(found->second.stride) * found->second.count;
    Diligent::BufferDesc desc;
    desc.Name = "Custom Readback";
    desc.Usage = Diligent::USAGE_STAGING;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    desc.Size = size;
    RefCntAutoPtr<Diligent::IBuffer> staging;
    m_data->device->CreateBuffer(desc, nullptr, &staging);
    if (!staging) return out;
    m_data->context->CopyBuffer(found->second.buffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, size, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_data->context->WaitForIdle();
    Diligent::MapHelper<uint8_t> mapped(m_data->context, staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
    const auto* bytes = static_cast<const uint8_t*>(mapped);
    if (!bytes) return out;
    out.assign(bytes, bytes + size);
    return out;
}

void CustomPasses::setParam(PassKind, uint32_t id, int index, const glm::vec4& value) {
    if (!m_data || id >= m_data->passes.size() || index < 0 || index >= kParamCount) return;
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
    out.reserve(m_data->passes.size());
    for (const Pass& pass : m_data->passes) out.push_back(pass.info);
    return out;
}
