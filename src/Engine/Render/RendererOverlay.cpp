module;

#include "RendererPrelude.hpp"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

module Engine.renderer;

import Engine.glm;
import Engine.camera;
import Engine.Render.entity;
import Engine.Render.scenedatabase;
import Engine.Render.component;
import Engine.mesh;
import Engine.Render.lighting;
import Engine.Render.custom;
import Engine.Render.settings;
import Engine.Render.material;
import Engine.Portal.view;
import Engine.Render.observation;

#include "RendererInternal.hpp"

namespace {

constexpr size_t kFloatsPerDebugVertex = sizeof(DebugLineVertex) / sizeof(float);
constexpr size_t kBytesPerPixel = 4;
constexpr unsigned char kOpaqueAlpha = 255;

struct DebugDepthUniforms {
    float nearPlane;
    float farPlane;
    float mipLevel;
    float padding;
};

std::vector<unsigned char> ConvertToRgba(const void* mappedData, size_t stride, const Diligent::TextureDesc& desc) {
    std::vector<unsigned char> pixels(static_cast<size_t>(desc.Width) * desc.Height * kBytesPerPixel);
    const bool bgra = desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM || desc.Format == Diligent::TEX_FORMAT_BGRA8_UNORM_SRGB;
    const size_t redOffset = bgra ? 2 : 0;
    const size_t blueOffset = bgra ? 0 : 2;
    for (Diligent::Uint32 row = 0; row < desc.Height; ++row) {
        const auto* source = static_cast<const unsigned char*>(mappedData) + stride * row;
        unsigned char* destination = pixels.data() + static_cast<size_t>(row) * desc.Width * kBytesPerPixel;
        for (Diligent::Uint32 column = 0; column < desc.Width; ++column) {
            const size_t offset = static_cast<size_t>(column) * kBytesPerPixel;
            destination[offset + 0] = source[offset + redOffset];
            destination[offset + 1] = source[offset + 1];
            destination[offset + 2] = source[offset + blueOffset];
            destination[offset + 3] = kOpaqueAlpha;
        }
    }
    return pixels;
}

}

void Renderer::captureBackBuffer() {
    if (m_screenshotPath.empty()) return;
    const std::string path = std::move(m_screenshotPath);
    m_screenshotPath.clear();
    auto* context = m_diligent->pImmediateContext.RawPtr();
    Diligent::ITexture* backBuffer = m_diligent->pSwapChain->GetCurrentBackBufferRTV()->GetTexture();
    const auto& desc = backBuffer->GetDesc();
    Diligent::TextureDesc stagingDesc;
    stagingDesc.Name = "Screenshot Staging";
    stagingDesc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    stagingDesc.Width = desc.Width;
    stagingDesc.Height = desc.Height;
    stagingDesc.Format = desc.Format;
    stagingDesc.Usage = Diligent::USAGE_STAGING;
    stagingDesc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
    Diligent::RefCntAutoPtr<Diligent::ITexture> staging;
    m_diligent->pDevice->CreateTexture(stagingDesc, nullptr, &staging);
    if (!staging) {
        Lit::Log::Error("Screenshot staging texture could not be created");
        return;
    }
    Diligent::CopyTextureAttribs copy;
    copy.pSrcTexture = backBuffer;
    copy.SrcTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    copy.pDstTexture = staging;
    copy.DstTextureTransitionMode = Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->WaitForIdle();
    Diligent::MappedTextureSubresource mapped;
    context->MapTextureSubresource(staging, 0, 0, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
    if (!mapped.pData) {
        Lit::Log::Error("Screenshot mapping failed");
        return;
    }
    const std::vector<unsigned char> pixels = ConvertToRgba(mapped.pData, mapped.Stride, desc);
    context->UnmapTextureSubresource(staging, 0, 0);
    if (stbi_write_png(path.c_str(), static_cast<int>(desc.Width), static_cast<int>(desc.Height), static_cast<int>(kBytesPerPixel), pixels.data(), static_cast<int>(desc.Width * kBytesPerPixel))) {
        Lit::Log::Info("Screenshot saved to {}", path);
    } else {
        Lit::Log::Error("Could not write screenshot {}", path);
    }
}

void Renderer::drawDebugDepth(const Camera& camera) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const DebugDepthUniforms uniforms{camera.getNearPlane(), camera.getFarPlane(), 0.0f, 0.0f};
    {
        Diligent::MapHelper<DebugDepthUniforms> mapped(gpu.pImmediateContext, gpu.pDebugDepthUniforms, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        *mapped = uniforms;
    }

    context->SetPipelineState(gpu.pDebugDepthPSO);
    if (auto* variable = gpu.pDebugDepthSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "u_depthTexture")) variable->Set(gpu.pHiZTextures[m_currentFrame]->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    if (auto* variable = gpu.pDebugDepthSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "DebugDepthUniforms")) variable->Set(gpu.pDebugDepthUniforms, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    context->CommitShaderResources(gpu.pDebugDepthSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawFullscreenTriangle(context);
}

void Renderer::drawDebugLines(const Camera& camera) {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    const size_t depthVertices = std::min(m_debugLines.size() / kFloatsPerDebugVertex, kMaxDebugVertices);
    const size_t overlayVertices = std::min(m_debugOverlayLines.size() / kFloatsPerDebugVertex, kMaxDebugVertices - depthVertices);
    auto uploadVertices = [&](const std::vector<float>& source, size_t firstVertex, size_t vertexCount) {
        if (vertexCount == 0) return;
        context->UpdateBuffer(gpu.pDebugLineVB, firstVertex * sizeof(DebugLineVertex), vertexCount * sizeof(DebugLineVertex), source.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    };
    uploadVertices(m_debugLines, 0, depthVertices);
    uploadVertices(m_debugOverlayLines, depthVertices, overlayVertices);
    {
        Diligent::MapHelper<glm::mat4> viewProjection(gpu.pImmediateContext, gpu.pDebugLineUBO, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        *viewProjection = camera.getProjectionMatrix() * camera.getViewMatrix();
    }
    Diligent::IBuffer* vertexBuffers[] = {gpu.pDebugLineVB};
    const Diligent::Uint64 offsets[] = {0};
    context->SetVertexBuffers(0, 1, vertexBuffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    if (depthVertices > 0) {
        context->SetPipelineState(gpu.pDebugLinePSO);
        context->CommitShaderResources(gpu.pDebugLineSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::DrawAttribs lineDraw;
        lineDraw.NumVertices = static_cast<Diligent::Uint32>(depthVertices);
        lineDraw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context->Draw(lineDraw);
    }
    if (overlayVertices > 0 && gpu.pDebugLineOverlayPSO && gpu.pDebugLineOverlaySRB) {
        context->SetPipelineState(gpu.pDebugLineOverlayPSO);
        context->CommitShaderResources(gpu.pDebugLineOverlaySRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Diligent::DrawAttribs overlayDraw;
        overlayDraw.NumVertices = static_cast<Diligent::Uint32>(overlayVertices);
        overlayDraw.StartVertexLocation = static_cast<Diligent::Uint32>(depthVertices);
        overlayDraw.Flags = Diligent::DRAW_FLAG_VERIFY_ALL;
        context->Draw(overlayDraw);
    }
}

void Renderer::drawOverlays(const Camera& camera) {
    auto& gpu = *m_diligent;
    if (m_debugDepthMode && gpu.pDebugDepthPSO && gpu.pDebugDepthSRB && gpu.pHiZTextures[m_currentFrame]) drawDebugDepth(camera);
    if ((!m_debugLines.empty() || !m_debugOverlayLines.empty()) && gpu.pDebugLinePSO && gpu.pDebugLineSRB) drawDebugLines(camera);
    m_debugLines.clear();
    m_debugOverlayLines.clear();

    if (m_fullProfiling && m_profilingHud) drawProfilingHud();

    m_uiManager->render();
    if (m_imgui) m_imgui->render();
    captureBackBuffer();
}

void Renderer::addDebugLine(const glm::vec3& from, const glm::vec3& to, const glm::vec4& color, bool overlay) {
    const DebugLineVertex vertices[2] = {
        {from, color},
        {to,   color}
    };
    const auto* raw = reinterpret_cast<const float*>(vertices);
    std::vector<float>& target = overlay ? m_debugOverlayLines : m_debugLines;
    target.insert(target.end(), raw, raw + 2 * kFloatsPerDebugVertex);
}

void Renderer::createDebugDepthPSO() {
    auto& gpu = *m_diligent;
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/debug_depth.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pixelShader = CompileShader(gpu.pDevice, "resources/shaders/debug_depth.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!vertexShader || !pixelShader) return;

    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = "Debug Depth PSO";
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    createInfo.GraphicsPipeline.NumRenderTargets = 1;
    createInfo.GraphicsPipeline.RTVFormats[0] = gpu.pSwapChain->GetDesc().ColorBufferFormat;
    createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    createInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    createInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;

    const auto variables = DescribeVariables({vertexShader, pixelShader});
    UseVariables(createInfo.PSODesc.ResourceLayout, variables);

    const Diligent::SamplerDesc pointClamp{Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP};
    Diligent::ImmutableSamplerDesc samplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "u_depthTexture", pointClamp}
    };
    createInfo.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    createInfo.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Diligent::Uint32>(std::size(samplers));

    gpu.pDevice->CreateGraphicsPipelineState(createInfo, &gpu.pDebugDepthPSO);
    if (!gpu.pDebugDepthPSO) {
        Lit::Log::Error("Failed to create Debug Depth PSO");
        return;
    }

    gpu.pDebugDepthUniforms = CreateUniformBuffer(gpu.pDevice, "Debug Depth Uniforms", sizeof(DebugDepthUniforms));
    gpu.pDebugDepthPSO->CreateShaderResourceBinding(&gpu.pDebugDepthSRB, true);
}

void Renderer::createDepthUpscalePSO() {
    auto& gpu = *m_diligent;
    gpu.pDepthUpscalePSO.Release();
    gpu.pDepthUpscaleSRB.Release();
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/debug_depth.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pixelShader = CompileShader(gpu.pDevice, "resources/shaders/depth_upscale.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!vertexShader || !pixelShader) return;

    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = "Depth Upscale PSO";
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    createInfo.GraphicsPipeline.NumRenderTargets = 0;
    createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    createInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = true;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_ALWAYS;
    createInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;

    const auto variables = DescribeVariables({vertexShader, pixelShader});
    UseVariables(createInfo.PSODesc.ResourceLayout, variables);

    const Diligent::SamplerDesc pointClamp{Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::FILTER_TYPE_POINT, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP, Diligent::TEXTURE_ADDRESS_CLAMP};
    Diligent::ImmutableSamplerDesc samplers[] = {
        {Diligent::SHADER_TYPE_PIXEL, "u_depthTexture", pointClamp}
    };
    createInfo.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    createInfo.PSODesc.ResourceLayout.NumImmutableSamplers = static_cast<Diligent::Uint32>(std::size(samplers));

    gpu.pDevice->CreateGraphicsPipelineState(createInfo, &gpu.pDepthUpscalePSO);
    if (!gpu.pDepthUpscalePSO) {
        Lit::Log::Error("Failed to create Depth Upscale PSO");
        return;
    }
    gpu.pDepthUpscalePSO->CreateShaderResourceBinding(&gpu.pDepthUpscaleSRB, true);
}

void Renderer::resolveOverlayDepth() {
    auto& gpu = *m_diligent;
    auto* context = gpu.pImmediateContext.RawPtr();
    auto* overlayView = gpu.pOverlayDepth->GetDefaultView(Diligent::TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(0, nullptr, overlayView, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(gpu.pDepthUpscalePSO);
    if (auto* variable = gpu.pDepthUpscaleSRB->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "u_depthTexture")) variable->Set(gpu.pSceneDepth->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    context->CommitShaderResources(gpu.pDepthUpscaleSRB, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawFullscreenTriangle(context);
}

void Renderer::createDebugLinePSO() {
    auto& gpu = *m_diligent;
    auto vertexShader = CompileShader(gpu.pDevice, "resources/shaders/debug_line.vert", Diligent::SHADER_TYPE_VERTEX);
    auto pixelShader = CompileShader(gpu.pDevice, "resources/shaders/debug_line.frag", Diligent::SHADER_TYPE_PIXEL);
    if (!vertexShader || !pixelShader) return;

    Diligent::GraphicsPipelineStateCreateInfo createInfo;
    createInfo.PSODesc.Name = "Debug Line PSO";
    createInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    createInfo.GraphicsPipeline.NumRenderTargets = 1;
    createInfo.GraphicsPipeline.RTVFormats[0] = gpu.pSwapChain->GetDesc().ColorBufferFormat;
    createInfo.GraphicsPipeline.DSVFormat = kSceneDepthFormat;
    createInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_LINE_LIST;
    createInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = true;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthWriteEnable = false;
    createInfo.GraphicsPipeline.DepthStencilDesc.DepthFunc = Diligent::COMPARISON_FUNC_LESS_EQUAL;
    createInfo.pVS = vertexShader;
    createInfo.pPS = pixelShader;

    Diligent::LayoutElement layoutElements[] = {
        Diligent::LayoutElement{0, 0, 3, Diligent::VT_FLOAT32, false},
        Diligent::LayoutElement{1, 0, 4, Diligent::VT_FLOAT32, false}
    };
    createInfo.GraphicsPipeline.InputLayout.LayoutElements = layoutElements;
    createInfo.GraphicsPipeline.InputLayout.NumElements = static_cast<Diligent::Uint32>(std::size(layoutElements));

    const auto variables = DescribeVariables({vertexShader, pixelShader});
    UseVariables(createInfo.PSODesc.ResourceLayout, variables);

    gpu.pDebugLineUBO = CreateUniformBuffer(gpu.pDevice, "Debug Line Uniforms", sizeof(glm::mat4));

    Diligent::BufferDesc vertexBufferDesc;
    vertexBufferDesc.Name = "Debug Line Vertices";
    vertexBufferDesc.Usage = Diligent::USAGE_DEFAULT;
    vertexBufferDesc.BindFlags = Diligent::BIND_VERTEX_BUFFER;
    vertexBufferDesc.Size = sizeof(DebugLineVertex) * kMaxDebugVertices;
    gpu.pDevice->CreateBuffer(vertexBufferDesc, nullptr, &gpu.pDebugLineVB);

    auto createLinePipeline = [&](const char* name, bool depthTest, Diligent::RefCntAutoPtr<Diligent::IPipelineState>& pipeline, Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding>& binding) {
        createInfo.PSODesc.Name = name;
        createInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = depthTest;
        gpu.pDevice->CreateGraphicsPipelineState(createInfo, &pipeline);
        if (!pipeline) {
            Lit::Log::Error("Failed to create {}", name);
            return;
        }
        pipeline->CreateShaderResourceBinding(&binding, true);
        if (auto* variable = binding->GetVariableByName(Diligent::SHADER_TYPE_VERTEX, "DebugLineUniforms")) variable->Set(gpu.pDebugLineUBO);
    };
    createLinePipeline("Debug Line PSO", true, gpu.pDebugLinePSO, gpu.pDebugLineSRB);
    createLinePipeline("Debug Line Overlay PSO", false, gpu.pDebugLineOverlayPSO, gpu.pDebugLineOverlaySRB);
}
