module;

#include "RendererPrelude.hpp"

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

constexpr double kMillisecondsPerSecond = 1000.0;
constexpr size_t kPassesPerLogLine = 4;
constexpr float kHudStartY = 120.0f;
constexpr float kHudLineStep = 18.0f;
constexpr float kHudTextScale = 0.4f;
constexpr float kHudLeft = 10.0f;
constexpr size_t kMatrixRows = 3;
constexpr size_t kMatrixColumns = 4;
constexpr size_t kWorldFloatsPerEntity = kMatrixRows * kMatrixColumns;
constexpr size_t kSphereFloats = 4;
constexpr float kMaxCustomDelta = 0.25f;

double ElapsedMilliseconds(const Diligent::QueryDataTimestamp& start, const Diligent::QueryDataTimestamp& end) {
    if (end.Counter <= start.Counter || end.Frequency == 0) return 0.0;
    return static_cast<double>(end.Counter - start.Counter) / static_cast<double>(end.Frequency) * kMillisecondsPerSecond;
}

}

void ReadGpuTimings(DiligentData& gpu, int frame) {
    gpu.gpuTotalMilliseconds = 0.0;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        gpu.passMilliseconds[pass] = 0.0;
        if (!gpu.passRecorded[frame][pass]) continue;
        auto& startQuery = gpu.passQueries[pass][frame][0];
        auto& endQuery = gpu.passQueries[pass][frame][1];
        if (!startQuery || !endQuery) continue;
        Diligent::QueryDataTimestamp start{};
        Diligent::QueryDataTimestamp end{};
        if (!startQuery->GetData(&start, sizeof(start), false) || !endQuery->GetData(&end, sizeof(end), false)) continue;
        gpu.passMilliseconds[pass] = ElapsedMilliseconds(start, end);
        gpu.gpuTotalMilliseconds += gpu.passMilliseconds[pass];
    }
}

void LogGpuTimings(const DiligentData& gpu) {
    Lit::Log::Info("GPU Total: {:.2f} ms | CPU Fence Wait: {:.2f} ms", gpu.gpuTotalMilliseconds, gpu.fenceWaitMilliseconds);
    std::string line;
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) {
        line += std::format("{}{}: {:.2f} ms", line.empty() ? "  " : " | ", kGpuPassNames[pass], gpu.passMilliseconds[pass]);
        if (pass % kPassesPerLogLine == kPassesPerLogLine - 1 || pass + 1 == kGpuPassCount) {
            Lit::Log::Info("{}", line);
            line.clear();
        }
    }
}

void Renderer::drawProfilingHud() {
    const auto& gpu = *m_diligent;
    float positionY = kHudStartY;
    const glm::vec3 headerColor(0.3f, 1.0f, 0.5f);
    const glm::vec3 textColor(0.9f, 0.9f, 0.9f);
    const glm::vec3 waitColor(1.0f, 0.4f, 0.4f);

    auto addLine = [&](const glm::vec3& color, std::string_view label, double milliseconds) {
        m_uiManager->addText(std::format("{}{:.3f} ms", label, milliseconds), kHudLeft, positionY, kHudTextScale, color);
        positionY += kHudLineStep;
    };
    m_uiManager->addText("--- Profiling HUD ---", kHudLeft, positionY, kHudTextScale, headerColor);
    positionY += kHudLineStep;
    addLine(waitColor, "CPU Fence Wait: ", gpu.fenceWaitMilliseconds);
    addLine(headerColor, "GPU Total: ", gpu.gpuTotalMilliseconds);
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) addLine(textColor, std::format("  {}: ", kGpuPassNames[pass]), gpu.passMilliseconds[pass]);
    for (const PassInfo& pass : m_custom.passes()) {
        if (!pass.enabled || !pass.valid) continue;
        const char* kindName = pass.kind == PassKind::Compute ? "compute" : pass.kind == PassKind::Draw ? "draw" : pass.stage == EffectStage::Ldr ? "ldr effect" : "effect";
        addLine(textColor, std::format("  {} ({}): ", pass.name, kindName), pass.gpuMilliseconds);
    }
}

std::vector<GpuEntityState> Renderer::readGpuEntityStates(size_t count) {
    count = std::min(count, m_maxObjects);
    std::vector<GpuEntityState> states(count);
    if (!m_initialized || count == 0) return states;

    auto* context = m_diligent->pImmediateContext.RawPtr();
    auto readBack = [&](Diligent::IBuffer* source, size_t bytes, auto&& consume) {
        Diligent::BufferDesc desc;
        desc.Name = "Readback";
        desc.Usage = Diligent::USAGE_STAGING;
        desc.CPUAccessFlags = Diligent::CPU_ACCESS_READ;
        desc.Size = bytes;
        Diligent::RefCntAutoPtr<Diligent::IBuffer> staging;
        m_diligent->pDevice->CreateBuffer(desc, nullptr, &staging);
        if (!staging) return;
        context->CopyBuffer(source, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, staging, 0, bytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->WaitForIdle();
        Diligent::MapHelper<float> mapped(context, staging, Diligent::MAP_READ, Diligent::MAP_FLAG_DO_NOT_WAIT);
        if (!mapped) return;
        consume(static_cast<const float*>(mapped));
    };

    readBack(m_diligent->pWorldMatrixBuffer[0], count * sizeof(glm::vec4) * kMatrixRows, [&](const float* rows) {
        for (size_t entityIndex = 0; entityIndex < count; ++entityIndex) {
            glm::mat4 world(1.0f);
            for (size_t row = 0; row < kMatrixRows; ++row) {
                for (size_t column = 0; column < kMatrixColumns; ++column) world[column][row] = rows[entityIndex * kWorldFloatsPerEntity + row * kMatrixColumns + column];
            }
            states[entityIndex].world = world;
        }
    });
    readBack(m_diligent->pCullSphereBuffer[0], count * sizeof(glm::vec4), [&](const float* spheres) {
        for (size_t entityIndex = 0; entityIndex < count; ++entityIndex) {
            const float* sphere = spheres + entityIndex * kSphereFloats;
            states[entityIndex].cullSphere = glm::vec4(sphere[0], sphere[1], sphere[2], sphere[3]);
        }
    });
    return states;
}

std::vector<GpuPassTiming> Renderer::gpuTimings() const {
    std::vector<GpuPassTiming> timings;
    if (!m_diligent) return timings;
    timings.reserve(kGpuPassCount + m_custom.passes().size());
    for (size_t pass = 0; pass < kGpuPassCount; ++pass) timings.push_back({kGpuPassNames[pass], m_diligent->passMilliseconds[pass]});
    for (const PassInfo& pass : m_custom.passes()) {
        if (pass.enabled && pass.valid) timings.push_back({pass.name, pass.gpuMilliseconds});
    }
    return timings;
}

double Renderer::gpuTotalMilliseconds() const { return m_diligent ? m_diligent->gpuTotalMilliseconds : 0.0; }

CustomFrame Renderer::customFrame() const {
    const auto& gpu = *m_diligent;
    CustomFrame frame;
    frame.frameSlot = m_currentFrame;
    frame.parity = static_cast<uint32_t>(m_lighting.hdrIndex());
    frame.time = static_cast<float>(glfwGetTime());
    frame.deltaTime = std::clamp(m_lastDeltaTime, 0.0f, kMaxCustomDelta);
    frame.frame = m_frameCounter;
    frame.width = m_renderWidth;
    frame.height = m_renderHeight;
    frame.profile = m_fullProfiling;
    frame.resources = {
        {"SceneData",         gpu.pSceneUBO[m_currentFrame]         },
        {"WorldMatrixBuffer", gpu.pWorldMatrixBuffer[m_currentFrame]},
        {"CullSphereBuffer",  gpu.pCullSphereBuffer[m_currentFrame] },
        {"RenderableBuffer",  gpu.pRenderableBuffer[m_currentFrame] },
        {"MaterialBuffer",    m_lighting.materialBuffer()           },
        {"LightBuffer",       m_lighting.lightBuffer()              },
        {"ExposureBuffer",    m_lighting.exposureBuffer()           },
        {"u_depth",           litgpu::srv(gpu.pSceneDepth)          },
        {"u_gbuffer0",        litgpu::srv(m_lighting.gbuffer(0))    },
        {"u_gbuffer1",        litgpu::srv(m_lighting.gbuffer(1))    },
        {"u_colorTextures",   m_lighting.colorTextures()            },
        {"u_dataTextures",    m_lighting.dataTextures()             },
        {"u_hdr",             litgpu::uav(m_lighting.hdr())         }
    };
    return frame;
}
