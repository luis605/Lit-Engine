module;

#include "GpuHelpers.hpp"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Query.h"

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

std::vector<litgpu::Named> gatherSharedResources(const CustomFrame& frame, const std::unordered_map<std::string, UserBuffer>& buffers, const std::unordered_map<std::string, RefCntAutoPtr<Diligent::ITexture>>& textures) {
    std::vector<litgpu::Named> list;
    list.reserve(frame.resources.size() + buffers.size() + textures.size() + 2);
    for (const auto& [name, userBuffer] : buffers) list.push_back({name, userBuffer.buffer});
    for (const auto& [name, texture] : textures) list.push_back({name, texture});
    for (const NamedResource& resource : frame.resources) list.push_back({resource.name, resource.object});
    return list;
}

Diligent::IBuffer* indirectBuffer(const std::unordered_map<std::string, UserBuffer>& buffers, const std::string& name) {
    if (name.empty()) return nullptr;
    const auto found = buffers.find(name);
    return found == buffers.end() ? nullptr : found->second.buffer.RawPtr();
}

void updateParams(Diligent::IDeviceContext* context, Pass& pass, const CustomFrame& frame) {
    ParamBlock block{};
    for (int index = 0; index < kParamCount; ++index) block.params[index] = pass.info.params[index];
    block.time = glm::vec4(frame.time, frame.deltaTime, static_cast<float>(frame.frame), 0.0f);
    context->UpdateBuffer(pass.params, 0, sizeof(block), &block, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void setFrameViewport(Diligent::IDeviceContext* context, const CustomFrame& frame) {
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(frame.width);
    viewport.Height = static_cast<float>(frame.height);
    context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(frame.width), static_cast<Diligent::Uint32>(frame.height));
}

void dispatchIndirectCompute(Diligent::IDeviceContext* context, const Pass& pass, Diligent::IShaderResourceBinding* binding, Diligent::IBuffer* arguments) {
    context->SetPipelineState(pass.pso);
    context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Diligent::DispatchComputeIndirectAttribs attribs;
    attribs.pAttribsBuffer = arguments;
    attribs.DispatchArgsByteOffset = pass.compute.indirectOffset;
    attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->DispatchComputeIndirect(attribs);
}

void issueDraw(Diligent::IDeviceContext* context, const Pass& pass, Diligent::IBuffer* arguments) {
    const DrawDesc& draw = pass.draw;
    if (arguments && pass.mesh) {
        Diligent::DrawMeshIndirectAttribs attribs;
        attribs.pAttribsBuffer = arguments;
        attribs.DrawArgsOffset = draw.indirectOffset;
        attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->DrawMeshIndirect(attribs);
    } else if (arguments) {
        Diligent::DrawIndirectAttribs attribs;
        attribs.pAttribsBuffer = arguments;
        attribs.DrawArgsOffset = draw.indirectOffset;
        attribs.AttribsBufferStateTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->DrawIndirect(attribs);
    } else if (pass.mesh) {
        Diligent::DrawMeshAttribs attribs;
        attribs.ThreadGroupCountX = static_cast<Diligent::Uint32>(std::max(draw.meshGroups.x, 1));
        attribs.ThreadGroupCountY = static_cast<Diligent::Uint32>(std::max(draw.meshGroups.y, 1));
        attribs.ThreadGroupCountZ = static_cast<Diligent::Uint32>(std::max(draw.meshGroups.z, 1));
        context->DrawMesh(attribs);
    } else {
        Diligent::DrawAttribs attribs;
        attribs.NumVertices = draw.vertexCount;
        attribs.NumInstances = std::max(draw.instanceCount, 1u);
        context->Draw(attribs);
    }
}

}

void CustomPasses::runCompute(ComputeStage stage, const CustomFrame& frame) {
    if (!m_data) return;
    std::vector<litgpu::Named> resources = gatherSharedResources(frame, m_data->buffers, m_data->textures);
    const size_t sharedCount = resources.size();
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Compute || !pass.info.enabled || !pass.pso || pass.compute.stage != stage) continue;
        updateParams(m_data->context, pass, frame);
        resources.erase(resources.begin() + static_cast<std::ptrdiff_t>(sharedCount), resources.end());
        resources.push_back({"LitParams", pass.params});
        Diligent::IShaderResourceBinding* binding = pass.bindings.get(pass.pso, resources);
        if (!binding) continue;
        const PassTimer timer(m_data->device, m_data->context, pass, frame);
        if (Diligent::IBuffer* arguments = indirectBuffer(m_data->buffers, pass.compute.indirectBuffer)) {
            dispatchIndirectCompute(m_data->context, pass, binding, arguments);
            continue;
        }
        const glm::ivec3 groups = glm::max(pass.compute.groups, glm::ivec3(1));
        litgpu::dispatch(m_data->context, pass.pso, binding, static_cast<Diligent::Uint32>(groups.x), static_cast<Diligent::Uint32>(groups.y), static_cast<Diligent::Uint32>(groups.z));
    }
}

void CustomPasses::runDraws(const CustomFrame& frame, Diligent::ITextureView* colorTarget, Diligent::ITextureView* depthTarget) {
    if (!m_data) return;
    std::vector<litgpu::Named> resources = gatherSharedResources(frame, m_data->buffers, m_data->textures);
    const size_t sharedCount = resources.size();
    bool targetsBound = false;
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Draw || !pass.info.enabled || !pass.pso) continue;
        updateParams(m_data->context, pass, frame);
        resources.erase(resources.begin() + static_cast<std::ptrdiff_t>(sharedCount), resources.end());
        resources.push_back({"LitParams", pass.params});
        Diligent::IShaderResourceBinding* binding = pass.bindings.get(pass.pso, resources);
        if (!binding) continue;
        if (!targetsBound) {
            m_data->context->SetRenderTargets(1, &colorTarget, depthTarget, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            setFrameViewport(m_data->context, frame);
            targetsBound = true;
        }
        const PassTimer timer(m_data->device, m_data->context, pass, frame);
        m_data->context->SetPipelineState(pass.pso);
        m_data->context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        issueDraw(m_data->context, pass, indirectBuffer(m_data->buffers, pass.draw.indirectBuffer));
    }
}

int CustomPasses::runEffects(const CustomFrame& frame, Diligent::ITexture* const targets[2], int current, EffectStage stage) {
    if (!m_data) return current;
    std::vector<litgpu::Named> resources = gatherSharedResources(frame, m_data->buffers, m_data->textures);
    const size_t sharedCount = resources.size();
    for (Pass& pass : m_data->passes) {
        if (pass.info.kind != PassKind::Effect || !pass.info.enabled || !pass.pso || pass.info.stage != stage) continue;
        updateParams(m_data->context, pass, frame);
        resources.erase(resources.begin() + static_cast<std::ptrdiff_t>(sharedCount), resources.end());
        resources.push_back({"LitParams", pass.params});
        resources.push_back({"u_color", litgpu::srv(targets[current])});
        Diligent::IShaderResourceBinding* binding = pass.bindings.get(pass.pso, resources);
        if (!binding) continue;
        Diligent::ITextureView* renderTarget = litgpu::rtv(targets[1 - current]);
        m_data->context->SetRenderTargets(1, &renderTarget, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        setFrameViewport(m_data->context, frame);
        const PassTimer timer(m_data->device, m_data->context, pass, frame);
        m_data->context->SetPipelineState(pass.pso);
        m_data->context->CommitShaderResources(binding, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        m_data->context->Draw(Diligent::DrawAttribs{3, Diligent::DRAW_FLAG_NONE});
        current = 1 - current;
    }
    return current;
}

void CustomPasses::runLdrEffects(const CustomFrame& frame, Diligent::ITexture* ldr) {
    if (!m_data || !ldr || !hasEffects(EffectStage::Ldr)) return;
    const auto& desc = ldr->GetDesc();
    const bool scratchMatches = m_data->ldrScratch && m_data->ldrScratch->GetDesc().Width == desc.Width && m_data->ldrScratch->GetDesc().Height == desc.Height;
    if (!scratchMatches) {
        m_data->ldrScratch = litgpu::texture(m_data->device, litgpu::TextureSpec{"Custom LDR Effect Target", Diligent::RESOURCE_DIM_TEX_2D, desc.Width, desc.Height, 1, 1, desc.Format, Diligent::BIND_SHADER_RESOURCE | Diligent::BIND_RENDER_TARGET});
        if (!m_data->ldrScratch) return;
    }
    Diligent::ITexture* const targets[2] = {ldr, m_data->ldrScratch};
    if (runEffects(frame, targets, 0, EffectStage::Ldr) == 0) return;
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = m_data->ldrScratch;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = ldr;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    m_data->context->CopyTexture(copy);
}
