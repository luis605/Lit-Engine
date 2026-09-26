module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/Texture.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/SwapChain.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/PipelineState.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/ShaderResourceBinding.h"
#include "DiligentCore/Common/interface/RefCntAutoPtr.hpp"
#include "DiligentCore/Graphics/GraphicsTools/interface/MapHelper.hpp"

#include "Engine/Log/Log.hpp"
#include "Engine/Render/GpuHelpers.hpp"

module Engine.UI.imgui;

namespace {

constexpr const char* kVertexSource = R"(
layout(location = 0) in vec2 in_position;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(std140) uniform ImGuiConstants {
    mat4 u_projection;
};
layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_color;
void main() {
    out_uv = in_uv;
    out_color = in_color;
    gl_Position = u_projection * vec4(in_position, 0.0, 1.0);
}
)";

constexpr const char* kPixelSource = R"(
layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_color;
layout(location = 0) out vec4 out_color;
uniform sampler2D u_texture;
void main() {
    out_color = in_color * texture(u_texture, in_uv);
}
)";

}  // namespace

struct ImGuiLayer::Data {
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapChain;
    Diligent::RefCntAutoPtr<Diligent::IPipelineState> pso;
    Diligent::RefCntAutoPtr<Diligent::IShaderResourceBinding> srb;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> constants;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> vertexBuffer;
    Diligent::RefCntAutoPtr<Diligent::IBuffer> indexBuffer;
    Diligent::Uint64 vertexCapacity = 0;
    Diligent::Uint64 indexCapacity = 0;
    GLFWwindow* window = nullptr;
    std::string iniPath;
};

ImGuiLayer::ImGuiLayer() = default;

ImGuiLayer::~ImGuiLayer() { shutdown(); }

bool ImGuiLayer::init(GLFWwindow* window, Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, Diligent::ISwapChain* swapChain, Diligent::TEXTURE_FORMAT depthFormat) {
    if (m_data) return true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendRendererName = "lit_diligent";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;

    auto* d = new Data();
    d->device = device;
    d->context = context;
    d->swapChain = swapChain;
    d->window = window;
    d->iniPath = "resources/editor_layout.ini";
    io.IniFilename = d->iniPath.c_str();

    float scaleX = 1.0f, scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    const float scale = std::clamp(std::max(scaleX, scaleY), 1.0f, 3.0f);
    if (std::filesystem::exists("resources/fonts/Inter.ttf")) {
        ImFontConfig config;
        config.OversampleH = 2;
        io.Fonts->AddFontFromFileTTF("resources/fonts/Inter.ttf", 16.0f * scale, &config);
    }
    ImGui::GetStyle().ScaleAllSizes(scale);

    Diligent::ShaderCreateInfo shaderCI;
    shaderCI.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    shaderCI.Desc.UseCombinedTextureSamplers = true;
    Diligent::RefCntAutoPtr<Diligent::IShader> vs;
    Diligent::RefCntAutoPtr<Diligent::IShader> ps;
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderCI.Desc.Name = "ImGui VS";
    shaderCI.Source = kVertexSource;
    vs = litgpu::createShader(device, shaderCI);
    shaderCI.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderCI.Desc.Name = "ImGui PS";
    shaderCI.Source = kPixelSource;
    ps = litgpu::createShader(device, shaderCI);
    if (!vs || !ps) {
        Lit::Log::Error("Failed to compile ImGui shaders");
        delete d;
        ImGui::DestroyContext();
        return false;
    }

    Diligent::GraphicsPipelineStateCreateInfo psoCI;
    psoCI.PSODesc.Name = "ImGui PSO";
    psoCI.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    psoCI.GraphicsPipeline.NumRenderTargets = 1;
    psoCI.GraphicsPipeline.RTVFormats[0] = swapChain->GetDesc().ColorBufferFormat;
    psoCI.GraphicsPipeline.DSVFormat = depthFormat;
    psoCI.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    psoCI.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    psoCI.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
    psoCI.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    auto& blend = psoCI.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    psoCI.pVS = vs;
    psoCI.pPS = ps;

    const Diligent::LayoutElement layout[] = {
        {0, 0, 2, Diligent::VT_FLOAT32, false},
        {1, 0, 2, Diligent::VT_FLOAT32, false},
        {2, 0, 4, Diligent::VT_UINT8, true},
    };
    psoCI.GraphicsPipeline.InputLayout.LayoutElements = layout;
    psoCI.GraphicsPipeline.InputLayout.NumElements = 3;

    const Diligent::ShaderResourceVariableDesc variables[] = {{Diligent::SHADER_TYPE_PIXEL, "u_texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    psoCI.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    psoCI.PSODesc.ResourceLayout.Variables = variables;
    psoCI.PSODesc.ResourceLayout.NumVariables = 1;
    Diligent::SamplerDesc linearClamp;
    linearClamp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.AddressU = linearClamp.AddressV = linearClamp.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    const Diligent::ImmutableSamplerDesc samplers[] = {{Diligent::SHADER_TYPE_PIXEL, "u_texture", linearClamp}};
    psoCI.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    psoCI.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    device->CreateGraphicsPipelineState(psoCI, &d->pso);
    if (!d->pso) {
        Lit::Log::Error("Failed to create ImGui pipeline");
        delete d;
        ImGui::DestroyContext();
        return false;
    }

    Diligent::BufferDesc constantsDesc;
    constantsDesc.Name = "ImGui Constants";
    constantsDesc.Usage = Diligent::USAGE_DYNAMIC;
    constantsDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    constantsDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    constantsDesc.Size = sizeof(float) * 16;
    device->CreateBuffer(constantsDesc, nullptr, &d->constants);
    if (auto* var = d->pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "ImGuiConstants")) var->Set(d->constants);
    d->pso->CreateShaderResourceBinding(&d->srb, true);

    ImGui_ImplGlfw_InitForOther(window, true);
    m_data = d;
    Lit::Log::Info("ImGui {} initialized", IMGUI_VERSION);
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_data) return;
    if (m_frameActive) {
        ImGui::EndFrame();
        m_frameActive = false;
    }
    for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
        if (auto* texture = static_cast<Diligent::ITexture*>(tex->BackendUserData)) texture->Release();
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    delete m_data;
    m_data = nullptr;
}

void ImGuiLayer::newFrame() {
    if (!m_data) return;
    if (m_frameActive) ImGui::EndFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
    m_frameActive = true;
}

namespace {

void UpdateTexture(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, ImTextureData* tex) {
    if (tex->Status == ImTextureStatus_WantCreate) {
        Diligent::TextureDesc desc;
        desc.Name = "ImGui Texture";
        desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
        desc.Width = static_cast<Diligent::Uint32>(tex->Width);
        desc.Height = static_cast<Diligent::Uint32>(tex->Height);
        desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
        desc.Usage = Diligent::USAGE_DEFAULT;
        desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
        Diligent::TextureSubResData sub;
        sub.pData = tex->GetPixels();
        sub.Stride = static_cast<Diligent::Uint64>(tex->Width) * 4;
        Diligent::TextureData data;
        data.NumSubresources = 1;
        data.pSubResources = &sub;
        Diligent::ITexture* texture = nullptr;
        device->CreateTexture(desc, &data, &texture);
        if (!texture) return;
        tex->BackendUserData = texture;
        tex->SetTexID(reinterpret_cast<ImTextureID>(texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)));
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantUpdates) {
        auto* texture = static_cast<Diligent::ITexture*>(tex->BackendUserData);
        if (!texture) return;
        for (const ImTextureRect& r : tex->Updates) {
            Diligent::Box box(r.x, r.x + r.w, r.y, r.y + r.h);
            Diligent::TextureSubResData sub;
            sub.pData = tex->GetPixelsAt(r.x, r.y);
            sub.Stride = static_cast<Diligent::Uint64>(tex->Width) * 4;
            context->UpdateTexture(texture, 0, 0, box, sub, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        }
        tex->SetStatus(ImTextureStatus_OK);
    } else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0) {
        if (auto* texture = static_cast<Diligent::ITexture*>(tex->BackendUserData)) texture->Release();
        tex->BackendUserData = nullptr;
        tex->SetTexID(ImTextureID_Invalid);
        tex->SetStatus(ImTextureStatus_Destroyed);
    }
}

bool EnsureBuffer(Diligent::IRenderDevice* device, Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer, Diligent::Uint64& capacity, Diligent::Uint64 needed, Diligent::BIND_FLAGS bind, const char* name) {
    if (buffer && capacity >= needed) return true;
    capacity = std::max<Diligent::Uint64>(needed + needed / 2, 64 * 1024);
    Diligent::BufferDesc desc;
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = bind;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = capacity;
    buffer.Release();
    device->CreateBuffer(desc, nullptr, &buffer);
    return buffer != nullptr;
}

}  // namespace

void ImGuiLayer::render() {
    if (!m_data || !m_frameActive) return;
    m_frameActive = false;
    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    Data& d = *m_data;

    if (drawData->Textures) {
        for (ImTextureData* tex : *drawData->Textures) {
            if (tex->Status != ImTextureStatus_OK) UpdateTexture(d.device, d.context, tex);
        }
    }

    const int fbWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int fbHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (fbWidth <= 0 || fbHeight <= 0 || drawData->TotalVtxCount == 0) return;

    const Diligent::Uint64 vertexBytes = static_cast<Diligent::Uint64>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const Diligent::Uint64 indexBytes = static_cast<Diligent::Uint64>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!EnsureBuffer(d.device, d.vertexBuffer, d.vertexCapacity, vertexBytes, Diligent::BIND_VERTEX_BUFFER, "ImGui Vertices")) return;
    if (!EnsureBuffer(d.device, d.indexBuffer, d.indexCapacity, indexBytes, Diligent::BIND_INDEX_BUFFER, "ImGui Indices")) return;
    {
        Diligent::MapHelper<ImDrawVert> vertices(d.context, d.vertexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        Diligent::MapHelper<ImDrawIdx> indices(d.context, d.indexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        ImDrawVert* vtx = vertices;
        ImDrawIdx* idx = indices;
        for (const ImDrawList* list : drawData->CmdLists) {
            std::memcpy(vtx, list->VtxBuffer.Data, static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
            std::memcpy(idx, list->IdxBuffer.Data, static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
            vtx += list->VtxBuffer.Size;
            idx += list->IdxBuffer.Size;
        }
    }

    {
        const float l = drawData->DisplayPos.x;
        const float r = drawData->DisplayPos.x + drawData->DisplaySize.x;
        const float t = drawData->DisplayPos.y;
        const float b = drawData->DisplayPos.y + drawData->DisplaySize.y;
        const float projection[16] = {2.0f / (r - l), 0.0f, 0.0f, 0.0f, 0.0f, 2.0f / (t - b), 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, (r + l) / (l - r), (t + b) / (b - t), 0.5f, 1.0f};
        Diligent::MapHelper<float> constants(d.context, d.constants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
        std::memcpy(static_cast<float*>(constants), projection, sizeof(projection));
    }

    auto setupState = [&] {
        Diligent::IBuffer* buffers[] = {d.vertexBuffer};
        const Diligent::Uint64 offsets[] = {0};
        d.context->SetVertexBuffers(0, 1, buffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
        d.context->SetIndexBuffer(d.indexBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        d.context->SetPipelineState(d.pso);
        Diligent::Viewport viewport;
        viewport.Width = static_cast<float>(fbWidth);
        viewport.Height = static_cast<float>(fbHeight);
        d.context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(fbWidth), static_cast<Diligent::Uint32>(fbHeight));
    };
    setupState();

    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    Diligent::Uint32 globalVertex = 0;
    Diligent::Uint32 globalIndex = 0;
    Diligent::ITextureView* boundTexture = nullptr;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback) {
                if (cmd.UserCallback == ImDrawCallback_ResetRenderState) {
                    setupState();
                    boundTexture = nullptr;
                } else {
                    cmd.UserCallback(list, &cmd);
                }
                continue;
            }
            const ImVec2 clipMin((cmd.ClipRect.x - clipOffset.x) * clipScale.x, (cmd.ClipRect.y - clipOffset.y) * clipScale.y);
            const ImVec2 clipMax((cmd.ClipRect.z - clipOffset.x) * clipScale.x, (cmd.ClipRect.w - clipOffset.y) * clipScale.y);
            const int x0 = std::max(0, static_cast<int>(clipMin.x));
            const int y0 = std::max(0, static_cast<int>(clipMin.y));
            const int x1 = std::min(fbWidth, static_cast<int>(clipMax.x));
            const int y1 = std::min(fbHeight, static_cast<int>(clipMax.y));
            if (x1 <= x0 || y1 <= y0) continue;
            const Diligent::Rect scissor(x0, y0, x1, y1);
            d.context->SetScissorRects(1, &scissor, static_cast<Diligent::Uint32>(fbWidth), static_cast<Diligent::Uint32>(fbHeight));

            auto* texture = reinterpret_cast<Diligent::ITextureView*>(cmd.GetTexID());
            if (!texture) continue;
            if (texture != boundTexture) {
                if (auto* var = d.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "u_texture")) var->Set(texture, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                d.context->CommitShaderResources(d.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                boundTexture = texture;
            }
            Diligent::DrawIndexedAttribs draw(cmd.ElemCount, sizeof(ImDrawIdx) == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_STATES);
            draw.FirstIndexLocation = cmd.IdxOffset + globalIndex;
            draw.BaseVertex = cmd.VtxOffset + globalVertex;
            d.context->DrawIndexed(draw);
        }
        globalIndex += static_cast<Diligent::Uint32>(list->IdxBuffer.Size);
        globalVertex += static_cast<Diligent::Uint32>(list->VtxBuffer.Size);
    }
    const Diligent::Rect full(0, 0, fbWidth, fbHeight);
    d.context->SetScissorRects(1, &full, static_cast<Diligent::Uint32>(fbWidth), static_cast<Diligent::Uint32>(fbHeight));
}
