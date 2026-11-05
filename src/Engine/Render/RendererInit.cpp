module;

#include "RendererPrelude.hpp"

#if defined(__linux__)
#undef Bool
#undef True
#undef False
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_GLX
#elif defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#define GLFW_EXPOSE_NATIVE_NSGL
#endif
#include <GLFW/glfw3native.h>

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

constexpr size_t kInitialVertexBufferBytes = 1024 * 1024 * 10;
constexpr size_t kInitialIndexBufferBytes = 1024 * 1024 * 4;
constexpr size_t kBytesPerObject = 300;
constexpr size_t kBudgetNumerator = 7;
constexpr size_t kBudgetDenominator = 10;
constexpr const char* kIgnoredValidationMessages[] = {
    "VUID-VkPhysicalDeviceMeshShaderFeaturesEXT-multiviewMeshShader-07032",
    "VUID-VkPhysicalDeviceMeshShaderFeaturesEXT-primitiveFragmentShadingRateMeshShader-07033"
};

}

Renderer::Renderer() = default;

Renderer::~Renderer() { cleanup(); }

void Renderer::createDevice() {
    auto& gpu = *m_diligent;
    gpu.pFactoryVk = Diligent::GetEngineFactoryVk();

    Diligent::EngineVkCreateInfo engineInfo;
    engineInfo.AdapterId = SelectFastestAdapter(gpu.pFactoryVk, engineInfo.GraphicsAPIVersion);
    engineInfo.Features.IndependentBlend = Diligent::DEVICE_FEATURE_STATE_ENABLED;
    engineInfo.Features.DepthClamp = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    engineInfo.Features.Tessellation = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    engineInfo.Features.GeometryShaders = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    engineInfo.Features.MeshShaders = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    engineInfo.Features.VariableRateShading = Diligent::DEVICE_FEATURE_STATE_OPTIONAL;
    engineInfo.ppIgnoreDebugMessageNames = kIgnoredValidationMessages;
    engineInfo.IgnoreDebugMessageCount = static_cast<Diligent::Uint32>(std::size(kIgnoredValidationMessages));

    gpu.pFactoryVk->CreateDeviceAndContextsVk(engineInfo, &gpu.pDevice, &gpu.pImmediateContext);
    Lit::Log::Phase("vulkan device");

    Diligent::SwapChainDesc swapChainDesc;
    swapChainDesc.ColorBufferFormat = Diligent::TEX_FORMAT_RGBA8_UNORM;
    swapChainDesc.DepthBufferFormat = Diligent::TEX_FORMAT_UNKNOWN;
    swapChainDesc.Usage = Diligent::SWAP_CHAIN_USAGE_RENDER_TARGET | Diligent::SWAP_CHAIN_USAGE_COPY_SOURCE;
    swapChainDesc.Width = m_windowWidth;
    swapChainDesc.Height = m_windowHeight;

    Diligent::NativeWindow nativeWindow;
#if defined(_WIN32)
    nativeWindow.hWnd = glfwGetWin32Window(m_window);
#elif defined(__linux__)
    nativeWindow.WindowId = static_cast<Diligent::Uint32>(glfwGetX11Window(m_window));
    nativeWindow.pDisplay = glfwGetX11Display();
#elif defined(__APPLE__)
    nativeWindow.pNSView = glfwGetCocoaWindow(m_window);
#endif

    gpu.pFactoryVk->CreateSwapChainVk(gpu.pDevice, gpu.pImmediateContext, swapChainDesc, nativeWindow, &gpu.pSwapChain);
    Lit::Log::Phase("swap chain");
}

void Renderer::createTimestampQueries() {
    auto& gpu = *m_diligent;
    Diligent::QueryDesc queryDesc;
    queryDesc.Type = Diligent::QUERY_TYPE_TIMESTAMP;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        queryDesc.Name = kGpuPassNames[pass];
        for (auto& frameQueries : gpu.passQueries[pass]) {
            for (auto& query : frameQueries) gpu.pDevice->CreateQuery(queryDesc, &query);
        }
    }
    queryDesc.Name = "Portal View";
    for (auto& frameQueries : gpu.portalQueries) {
        for (auto& viewQueries : frameQueries) {
            for (auto& query : viewQueries) gpu.pDevice->CreateQuery(queryDesc, &query);
        }
    }
}

void Renderer::createGeometryBuffers() {
    auto& gpu = *m_diligent;
    m_vboSize = kInitialVertexBufferBytes;
    m_eboSize = kInitialIndexBufferBytes;
    gpu.pVBO = CreateVertexBuffer(gpu.pDevice, m_vboSize);
    gpu.pEBO = CreateIndexBuffer(gpu.pDevice, m_eboSize);

    Diligent::SamplerDesc samplerDesc;
    samplerDesc.Name = "Hi-Z Sampler";
    samplerDesc.MinFilter = Diligent::FILTER_TYPE_POINT;
    samplerDesc.MagFilter = Diligent::FILTER_TYPE_POINT;
    samplerDesc.MipFilter = Diligent::FILTER_TYPE_POINT;
    samplerDesc.AddressU = Diligent::TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = Diligent::TEXTURE_ADDRESS_CLAMP;
    gpu.pDevice->CreateSampler(samplerDesc, &gpu.pHiZSampler);
}

void Renderer::createFences() {
    auto& gpu = *m_diligent;
    Diligent::FenceDesc fenceDesc;
    fenceDesc.Type = Diligent::FENCE_TYPE_CPU_WAIT_ONLY;
    for (int frameIndex = 0; frameIndex < DiligentData::NumFrames; ++frameIndex) {
        gpu.pDevice->CreateFence(fenceDesc, &gpu.pFences[frameIndex]);
        gpu.FenceValues[frameIndex] = 0;
    }
    gpu.CurrentFenceValue = 0;
}

void Renderer::init(GLFWwindow* window, const int windowWidth, const int windowHeight) {
    if (m_initialized) return;

    m_window = window;
    m_windowWidth = windowWidth;
    m_windowHeight = windowHeight;
    m_renderWidth = windowWidth;
    m_renderHeight = windowHeight;

    m_diligent = new DiligentData();
    createDevice();
    auto& gpu = *m_diligent;
    if (!std::getenv("LIT_NO_PREFETCH")) PrefetchShaders(gpu.pDevice);
    Lit::Log::Phase("shader prefetch");

    m_uiManager = new UIManager();
    m_uiManager->init(gpu.pDevice, gpu.pImmediateContext, gpu.pSwapChain, kSceneDepthFormat, windowWidth, windowHeight);
    Lit::Log::Phase("text ui");

    if (!m_lighting.init(gpu.pDevice, gpu.pImmediateContext, gpu.pSwapChain->GetDesc().ColorBufferFormat, windowWidth, windowHeight)) Lit::Log::Error("Lighting pipeline failed to initialize");
    Lit::Log::Phase("lighting");
    m_custom.init(gpu.pDevice, gpu.pImmediateContext);
    Lit::Log::Phase("custom passes");
    createPipelines();
    Lit::Log::Phase("scene pipelines");
    m_numDrawingShaders = gpu.surfaces.size();

    createTimestampQueries();
    createGeometryBuffers();
    createScreenTargets();
    if (!pipelinesReady()) Lit::Log::Error("Renderer pipelines failed to build; nothing will be drawn until the shader errors above are fixed.");

    reallocateBuffers(kInitialMaxObjects);
    createFences();

    Lit::Log::Phase("renderer buffers");
    m_initialized = true;
}

size_t Renderer::objectBudget() const {
    const auto localMemory = static_cast<size_t>(m_diligent->pDevice->GetAdapterInfo().Memory.LocalMemory);
    if (localMemory == 0) return std::numeric_limits<size_t>::max();
    return localMemory / kBudgetDenominator * kBudgetNumerator / kBytesPerObject;
}

bool Renderer::resizeToWindow() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    if (width <= 0 || height <= 0 || (width == m_windowWidth && height == m_windowHeight)) return false;

    m_diligent->pImmediateContext->WaitForIdle();
    m_diligent->pSwapChain->Resize(static_cast<Diligent::Uint32>(width), static_cast<Diligent::Uint32>(height));
    const auto& swapChainDesc = m_diligent->pSwapChain->GetDesc();
    m_windowWidth = static_cast<int>(swapChainDesc.Width);
    m_windowHeight = static_cast<int>(swapChainDesc.Height);
    applyRenderSize();
    m_uiManager->resize(m_windowWidth, m_windowHeight);
    return true;
}

void Renderer::present() {
    if (m_diligent && m_diligent->pSwapChain) m_diligent->pSwapChain->Present(0);
}

void Renderer::cleanup() {
    if (!m_initialized) return;

    m_diligent->pImmediateContext->WaitForIdle();
    for (auto& fence : m_diligent->pFences) fence.Release();

    delete m_imgui;
    m_imgui = nullptr;
    m_custom.shutdown();
    m_lighting.shutdown();
    delete m_uiManager;
    m_uiManager = nullptr;

    delete m_diligent;
    m_diligent = nullptr;

    m_initialized = false;
}

bool Renderer::enableImGui() {
    if (!m_initialized) return false;
    if (!m_imgui) m_imgui = new ImGuiLayer();
    return m_imgui->init(m_window, m_diligent->pDevice, m_diligent->pImmediateContext, m_diligent->pSwapChain, kSceneDepthFormat);
}

void Renderer::beginUi() {
    if (m_imgui) m_imgui->newFrame();
}

void Renderer::AddText(const std::string& text, float x, float y, float scale, const glm::vec3& color) {
    if (m_uiManager) m_uiManager->addText(text, x, y, scale, color);
}

void Renderer::setSmallObjectThreshold(float threshold) { m_smallObjectThreshold = threshold; }
void Renderer::setLodBias(float bias) { m_lodBias = std::max(bias, 0.0f); }
void Renderer::setForcedLod(int lod) { m_forcedLod = (lod < 0) ? -1 : std::min(lod, static_cast<int>(kLodLevelCount) - 1); }
int Renderer::getForcedLod() const { return m_forcedLod; }
void Renderer::setDebugDepthMode(bool enabled) { m_debugDepthMode = enabled; }
bool Renderer::isDebugDepthMode() const { return m_debugDepthMode; }
void Renderer::setFullProfiling(bool enabled) { m_fullProfiling = enabled; }

void Renderer::setAnimation(float time, uint32_t movingCount, uint32_t entityOffset) {
    m_animTime = time;
    m_animMovingCount = movingCount;
    m_animEntityOffset = entityOffset;
}

void Renderer::setMaterial(uint32_t index, const Material& material) {
    if (!m_initialized) return;
    m_lighting.setMaterial(index, material);
}
