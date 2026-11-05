module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
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

}

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

namespace {

constexpr float kBaseFontSize = 16.0f;
constexpr float kMaxUiScale = 3.0f;
constexpr Diligent::Uint64 kMinBufferBytes = 64 * 1024;
constexpr size_t kProjectionFloatCount = 16;

void configureFonts(GLFWwindow* window, ImGuiIO& io) {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(window, &scaleX, &scaleY);
    const float scale = std::clamp(std::max(scaleX, scaleY), 1.0f, kMaxUiScale);
    if (std::filesystem::exists("resources/fonts/Inter.ttf")) {
        ImFontConfig fontConfig;
        fontConfig.OversampleH = 2;
        io.Fonts->AddFontFromFileTTF("resources/fonts/Inter.ttf", kBaseFontSize * scale, &fontConfig);
    }
    ImGui::GetStyle().ScaleAllSizes(scale);
}

bool createShaders(Diligent::IRenderDevice* device, Diligent::RefCntAutoPtr<Diligent::IShader>& vertexShader, Diligent::RefCntAutoPtr<Diligent::IShader>& pixelShader) {
    Diligent::ShaderCreateInfo shaderInfo;
    shaderInfo.SourceLanguage = Diligent::SHADER_SOURCE_LANGUAGE_GLSL;
    shaderInfo.Desc.UseCombinedTextureSamplers = true;
    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_VERTEX;
    shaderInfo.Desc.Name = "ImGui VS";
    shaderInfo.Source = kVertexSource;
    vertexShader = litgpu::createShader(device, shaderInfo);
    shaderInfo.Desc.ShaderType = Diligent::SHADER_TYPE_PIXEL;
    shaderInfo.Desc.Name = "ImGui PS";
    shaderInfo.Source = kPixelSource;
    pixelShader = litgpu::createShader(device, shaderInfo);
    return vertexShader && pixelShader;
}

void createPipeline(ImGuiLayer::Data& data, Diligent::TEXTURE_FORMAT depthFormat, Diligent::IShader* vertexShader, Diligent::IShader* pixelShader) {
    Diligent::GraphicsPipelineStateCreateInfo pipelineInfo;
    pipelineInfo.PSODesc.Name = "ImGui PSO";
    pipelineInfo.PSODesc.PipelineType = Diligent::PIPELINE_TYPE_GRAPHICS;
    pipelineInfo.GraphicsPipeline.NumRenderTargets = 1;
    pipelineInfo.GraphicsPipeline.RTVFormats[0] = data.swapChain->GetDesc().ColorBufferFormat;
    pipelineInfo.GraphicsPipeline.DSVFormat = depthFormat;
    pipelineInfo.GraphicsPipeline.PrimitiveTopology = Diligent::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pipelineInfo.GraphicsPipeline.RasterizerDesc.CullMode = Diligent::CULL_MODE_NONE;
    pipelineInfo.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
    pipelineInfo.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    auto& blend = pipelineInfo.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true;
    blend.SrcBlend = Diligent::BLEND_FACTOR_SRC_ALPHA;
    blend.DestBlend = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOp = Diligent::BLEND_OPERATION_ADD;
    blend.SrcBlendAlpha = Diligent::BLEND_FACTOR_ONE;
    blend.DestBlendAlpha = Diligent::BLEND_FACTOR_INV_SRC_ALPHA;
    blend.BlendOpAlpha = Diligent::BLEND_OPERATION_ADD;
    pipelineInfo.pVS = vertexShader;
    pipelineInfo.pPS = pixelShader;

    const Diligent::LayoutElement layout[] = {
        {0, 0, 2, Diligent::VT_FLOAT32, false},
        {1, 0, 2, Diligent::VT_FLOAT32, false},
        {2, 0, 4, Diligent::VT_UINT8, true},
    };
    pipelineInfo.GraphicsPipeline.InputLayout.LayoutElements = layout;
    pipelineInfo.GraphicsPipeline.InputLayout.NumElements = 3;

    const Diligent::ShaderResourceVariableDesc variables[] = {{Diligent::SHADER_TYPE_PIXEL, "u_texture", Diligent::SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC}};
    pipelineInfo.PSODesc.ResourceLayout.DefaultVariableType = Diligent::SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    pipelineInfo.PSODesc.ResourceLayout.Variables = variables;
    pipelineInfo.PSODesc.ResourceLayout.NumVariables = 1;
    Diligent::SamplerDesc linearClamp;
    linearClamp.MinFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MagFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.MipFilter = Diligent::FILTER_TYPE_LINEAR;
    linearClamp.AddressU = linearClamp.AddressV = linearClamp.AddressW = Diligent::TEXTURE_ADDRESS_CLAMP;
    const Diligent::ImmutableSamplerDesc samplers[] = {{Diligent::SHADER_TYPE_PIXEL, "u_texture", linearClamp}};
    pipelineInfo.PSODesc.ResourceLayout.ImmutableSamplers = samplers;
    pipelineInfo.PSODesc.ResourceLayout.NumImmutableSamplers = 1;
    data.device->CreateGraphicsPipelineState(pipelineInfo, &data.pso);
}

void createConstants(ImGuiLayer::Data& data) {
    Diligent::BufferDesc constantsDesc;
    constantsDesc.Name = "ImGui Constants";
    constantsDesc.Usage = Diligent::USAGE_DYNAMIC;
    constantsDesc.BindFlags = Diligent::BIND_UNIFORM_BUFFER;
    constantsDesc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    constantsDesc.Size = sizeof(float) * kProjectionFloatCount;
    data.device->CreateBuffer(constantsDesc, nullptr, &data.constants);
    if (auto* variable = data.pso->GetStaticVariableByName(Diligent::SHADER_TYPE_VERTEX, "ImGuiConstants")) variable->Set(data.constants);
    data.pso->CreateShaderResourceBinding(&data.srb, true);
}

void releaseTexture(ImTextureData* textureData) {
    if (auto* texture = static_cast<Diligent::ITexture*>(textureData->BackendUserData)) texture->Release();
    textureData->BackendUserData = nullptr;
    textureData->SetTexID(ImTextureID_Invalid);
    textureData->SetStatus(ImTextureStatus_Destroyed);
}

void createTexture(Diligent::IRenderDevice* device, ImTextureData* textureData) {
    Diligent::TextureDesc desc;
    desc.Name = "ImGui Texture";
    desc.Type = Diligent::RESOURCE_DIM_TEX_2D;
    desc.Width = static_cast<Diligent::Uint32>(textureData->Width);
    desc.Height = static_cast<Diligent::Uint32>(textureData->Height);
    desc.Format = Diligent::TEX_FORMAT_RGBA8_UNORM;
    desc.Usage = Diligent::USAGE_DEFAULT;
    desc.BindFlags = Diligent::BIND_SHADER_RESOURCE;
    Diligent::TextureSubResData subresource;
    subresource.pData = textureData->GetPixels();
    subresource.Stride = static_cast<Diligent::Uint64>(textureData->Width) * 4;
    Diligent::TextureData initialData;
    initialData.NumSubresources = 1;
    initialData.pSubResources = &subresource;
    Diligent::ITexture* texture = nullptr;
    device->CreateTexture(desc, &initialData, &texture);
    if (!texture) return;
    textureData->BackendUserData = texture;
    textureData->SetTexID(reinterpret_cast<ImTextureID>(texture->GetDefaultView(Diligent::TEXTURE_VIEW_SHADER_RESOURCE)));
    textureData->SetStatus(ImTextureStatus_OK);
}

void updateTexture(Diligent::IDeviceContext* context, ImTextureData* textureData) {
    auto* texture = static_cast<Diligent::ITexture*>(textureData->BackendUserData);
    if (!texture) return;
    for (const ImTextureRect& rect : textureData->Updates) {
        const Diligent::Box box(rect.x, rect.x + rect.w, rect.y, rect.y + rect.h);
        Diligent::TextureSubResData subresource;
        subresource.pData = textureData->GetPixelsAt(rect.x, rect.y);
        subresource.Stride = static_cast<Diligent::Uint64>(textureData->Width) * 4;
        context->UpdateTexture(texture, 0, 0, box, subresource, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    }
    textureData->SetStatus(ImTextureStatus_OK);
}

void processTexture(Diligent::IRenderDevice* device, Diligent::IDeviceContext* context, ImTextureData* textureData) {
    if (textureData->Status == ImTextureStatus_WantCreate) createTexture(device, textureData);
    else if (textureData->Status == ImTextureStatus_WantUpdates) updateTexture(context, textureData);
    else if (textureData->Status == ImTextureStatus_WantDestroy && textureData->UnusedFrames > 0) releaseTexture(textureData);
}

bool ensureBuffer(Diligent::IRenderDevice* device, Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer, Diligent::Uint64& capacity, Diligent::Uint64 needed, Diligent::BIND_FLAGS bindFlags, const char* name) {
    if (buffer && capacity >= needed) return true;
    capacity = std::max<Diligent::Uint64>(needed + needed / 2, kMinBufferBytes);
    Diligent::BufferDesc desc;
    desc.Name = name;
    desc.Usage = Diligent::USAGE_DYNAMIC;
    desc.BindFlags = bindFlags;
    desc.CPUAccessFlags = Diligent::CPU_ACCESS_WRITE;
    desc.Size = capacity;
    buffer.Release();
    device->CreateBuffer(desc, nullptr, &buffer);
    return buffer != nullptr;
}

void uploadGeometry(ImGuiLayer::Data& data, const ImDrawData* drawData) {
    Diligent::MapHelper<ImDrawVert> vertices(data.context, data.vertexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    Diligent::MapHelper<ImDrawIdx> indices(data.context, data.indexBuffer, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    ImDrawVert* vertexCursor = vertices;
    ImDrawIdx* indexCursor = indices;
    for (const ImDrawList* list : drawData->CmdLists) {
        std::memcpy(vertexCursor, list->VtxBuffer.Data, static_cast<size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert));
        std::memcpy(indexCursor, list->IdxBuffer.Data, static_cast<size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx));
        vertexCursor += list->VtxBuffer.Size;
        indexCursor += list->IdxBuffer.Size;
    }
}

void uploadProjection(ImGuiLayer::Data& data, const ImDrawData* drawData) {
    const float left = drawData->DisplayPos.x;
    const float right = drawData->DisplayPos.x + drawData->DisplaySize.x;
    const float top = drawData->DisplayPos.y;
    const float bottom = drawData->DisplayPos.y + drawData->DisplaySize.y;
    const float projection[kProjectionFloatCount] = {2.0f / (right - left), 0.0f, 0.0f, 0.0f, 0.0f, 2.0f / (top - bottom), 0.0f, 0.0f, 0.0f, 0.0f, 0.5f, 0.0f, (right + left) / (left - right), (top + bottom) / (bottom - top), 0.5f, 1.0f};
    Diligent::MapHelper<float> constants(data.context, data.constants, Diligent::MAP_WRITE, Diligent::MAP_FLAG_DISCARD);
    std::memcpy(static_cast<float*>(constants), projection, sizeof(projection));
}

void setupRenderState(ImGuiLayer::Data& data, int framebufferWidth, int framebufferHeight) {
    Diligent::IBuffer* buffers[] = {data.vertexBuffer};
    const Diligent::Uint64 offsets[] = {0};
    data.context->SetVertexBuffers(0, 1, buffers, offsets, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, Diligent::SET_VERTEX_BUFFERS_FLAG_RESET);
    data.context->SetIndexBuffer(data.indexBuffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    data.context->SetPipelineState(data.pso);
    Diligent::Viewport viewport;
    viewport.Width = static_cast<float>(framebufferWidth);
    viewport.Height = static_cast<float>(framebufferHeight);
    data.context->SetViewports(1, &viewport, static_cast<Diligent::Uint32>(framebufferWidth), static_cast<Diligent::Uint32>(framebufferHeight));
}

void drawCommandLists(ImGuiLayer::Data& data, const ImDrawData* drawData, int framebufferWidth, int framebufferHeight) {
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;
    const auto framebufferWidthU = static_cast<Diligent::Uint32>(framebufferWidth);
    const auto framebufferHeightU = static_cast<Diligent::Uint32>(framebufferHeight);
    Diligent::Uint32 globalVertex = 0;
    Diligent::Uint32 globalIndex = 0;
    Diligent::ITextureView* boundTexture = nullptr;
    for (const ImDrawList* list : drawData->CmdLists) {
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback) {
                if (command.UserCallback == ImDrawCallback_ResetRenderState) {
                    setupRenderState(data, framebufferWidth, framebufferHeight);
                    boundTexture = nullptr;
                } else {
                    command.UserCallback(list, &command);
                }
                continue;
            }
            const int clipMinX = std::max(0, static_cast<int>((command.ClipRect.x - clipOffset.x) * clipScale.x));
            const int clipMinY = std::max(0, static_cast<int>((command.ClipRect.y - clipOffset.y) * clipScale.y));
            const int clipMaxX = std::min(framebufferWidth, static_cast<int>((command.ClipRect.z - clipOffset.x) * clipScale.x));
            const int clipMaxY = std::min(framebufferHeight, static_cast<int>((command.ClipRect.w - clipOffset.y) * clipScale.y));
            if (clipMaxX <= clipMinX || clipMaxY <= clipMinY) continue;
            const Diligent::Rect scissor(clipMinX, clipMinY, clipMaxX, clipMaxY);
            data.context->SetScissorRects(1, &scissor, framebufferWidthU, framebufferHeightU);

            auto* texture = reinterpret_cast<Diligent::ITextureView*>(command.GetTexID());
            if (!texture) continue;
            if (texture != boundTexture) {
                if (auto* variable = data.srb->GetVariableByName(Diligent::SHADER_TYPE_PIXEL, "u_texture")) variable->Set(texture, Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                data.context->CommitShaderResources(data.srb, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                boundTexture = texture;
            }
            Diligent::DrawIndexedAttribs draw(command.ElemCount, sizeof(ImDrawIdx) == 2 ? Diligent::VT_UINT16 : Diligent::VT_UINT32, Diligent::DRAW_FLAG_VERIFY_STATES);
            draw.FirstIndexLocation = command.IdxOffset + globalIndex;
            draw.BaseVertex = command.VtxOffset + globalVertex;
            data.context->DrawIndexed(draw);
        }
        globalIndex += static_cast<Diligent::Uint32>(list->IdxBuffer.Size);
        globalVertex += static_cast<Diligent::Uint32>(list->VtxBuffer.Size);
    }
    const Diligent::Rect fullScissor(0, 0, framebufferWidth, framebufferHeight);
    data.context->SetScissorRects(1, &fullScissor, framebufferWidthU, framebufferHeightU);
}

}

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

    auto data = std::make_unique<Data>();
    data->device = device;
    data->context = context;
    data->swapChain = swapChain;
    data->window = window;
    data->iniPath = "resources/editor_layout.ini";
    io.IniFilename = data->iniPath.c_str();
    configureFonts(window, io);

    Diligent::RefCntAutoPtr<Diligent::IShader> vertexShader;
    Diligent::RefCntAutoPtr<Diligent::IShader> pixelShader;
    if (!createShaders(device, vertexShader, pixelShader)) {
        Lit::Log::Error("Failed to compile ImGui shaders");
        ImGui::DestroyContext();
        return false;
    }
    createPipeline(*data, depthFormat, vertexShader, pixelShader);
    if (!data->pso) {
        Lit::Log::Error("Failed to create ImGui pipeline");
        ImGui::DestroyContext();
        return false;
    }
    createConstants(*data);

    ImGui_ImplGlfw_InitForOther(window, true);
    m_data = data.release();
    Lit::Log::Info("ImGui {} initialized", IMGUI_VERSION);
    return true;
}

void ImGuiLayer::shutdown() {
    if (!m_data) return;
    if (m_frameActive) {
        ImGui::EndFrame();
        m_frameActive = false;
    }
    for (ImTextureData* textureData : ImGui::GetPlatformIO().Textures) releaseTexture(textureData);
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

void ImGuiLayer::render() {
    if (!m_data || !m_frameActive) return;
    m_frameActive = false;
    ImGui::Render();
    const ImDrawData* drawData = ImGui::GetDrawData();
    Data& data = *m_data;

    if (drawData->Textures) {
        for (ImTextureData* textureData : *drawData->Textures) {
            if (textureData->Status != ImTextureStatus_OK) processTexture(data.device, data.context, textureData);
        }
    }

    const int framebufferWidth = static_cast<int>(drawData->DisplaySize.x * drawData->FramebufferScale.x);
    const int framebufferHeight = static_cast<int>(drawData->DisplaySize.y * drawData->FramebufferScale.y);
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || drawData->TotalVtxCount == 0) return;

    const Diligent::Uint64 vertexBytes = static_cast<Diligent::Uint64>(drawData->TotalVtxCount) * sizeof(ImDrawVert);
    const Diligent::Uint64 indexBytes = static_cast<Diligent::Uint64>(drawData->TotalIdxCount) * sizeof(ImDrawIdx);
    if (!ensureBuffer(data.device, data.vertexBuffer, data.vertexCapacity, vertexBytes, Diligent::BIND_VERTEX_BUFFER, "ImGui Vertices")) return;
    if (!ensureBuffer(data.device, data.indexBuffer, data.indexCapacity, indexBytes, Diligent::BIND_INDEX_BUFFER, "ImGui Indices")) return;
    uploadGeometry(data, drawData);
    uploadProjection(data, drawData);
    setupRenderState(data, framebufferWidth, framebufferHeight);
    drawCommandLists(data, drawData, framebufferWidth, framebufferHeight);
}
