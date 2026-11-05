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

struct MeshBounds {
    glm::vec3 center;
    float radius;
};

MeshBounds ComputeBounds(const Mesh& mesh) {
    glm::vec3 center(0.0f);
    const size_t vertexCount = mesh.vertices.size() / kVertexFloats;
    if (vertexCount == 0) return {center, 0.0f};

    for (size_t offset = 0; offset + 2 < mesh.vertices.size(); offset += kVertexFloats) {
        center.x += mesh.vertices[offset];
        center.y += mesh.vertices[offset + 1];
        center.z += mesh.vertices[offset + 2];
    }
    center /= static_cast<float>(vertexCount);

    float maxRadiusSquared = 0.0f;
    for (size_t offset = 0; offset + 2 < mesh.vertices.size(); offset += kVertexFloats) {
        const glm::vec3 vertex(mesh.vertices[offset], mesh.vertices[offset + 1], mesh.vertices[offset + 2]);
        maxRadiusSquared = std::max(maxRadiusSquared, glm::distance2(center, vertex));
    }
    return {center, glm::sqrt(maxRadiusSquared)};
}

float PackedShape(const Mesh& mesh, float boundingRadius) {
    const float silhouetteRatio = boundingRadius > 0.0f ? std::min(1.0f, std::sqrt(mesh.surfaceArea() / (4.0f * kPi)) / boundingRadius) : 1.0f;
    return mesh.isRound() ? -silhouetteRatio : silhouetteRatio;
}

}

void Renderer::growGeometryBuffers(size_t vertexDataSize, size_t indexDataSize) {
    auto& gpu = *m_diligent;
    if (gTotalVertexBytes + vertexDataSize <= m_vboSize && gTotalIndexBytes + indexDataSize <= m_eboSize) return;
    m_vboSize = std::max(m_vboSize * 2, gTotalVertexBytes + vertexDataSize);
    m_eboSize = std::max(m_eboSize * 2, gTotalIndexBytes + indexDataSize);

    auto resize = [&](Diligent::RefCntAutoPtr<Diligent::IBuffer>& buffer, size_t usedBytes, Diligent::RefCntAutoPtr<Diligent::IBuffer> replacement) {
        if (usedBytes > 0) gpu.pImmediateContext->CopyBuffer(buffer, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, replacement, 0, usedBytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        buffer = replacement;
    };
    resize(gpu.pVBO, gTotalVertexBytes, CreateVertexBuffer(gpu.pDevice, m_vboSize));
    resize(gpu.pEBO, gTotalIndexBytes, CreateIndexBuffer(gpu.pDevice, m_eboSize));
}

uint32_t Renderer::uploadMeshSlot(const Mesh& mesh) {
    auto& gpu = *m_diligent;
    if (gMeshInfos.size() >= kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached; refusing to upload another mesh.", kMaxMeshes);
        return kInvalidMeshId;
    }

    const size_t vertexDataSize = mesh.vertices.size() * sizeof(float);
    const size_t indexDataSize = mesh.indices.size() * sizeof(unsigned int);
    Lit::Log::Info("Uploading mesh: {} vertices ({} bytes), {} indices ({} bytes)", mesh.vertices.size() / kVertexFloats, vertexDataSize, mesh.indices.size(), indexDataSize);

    growGeometryBuffers(vertexDataSize, indexDataSize);
    if (vertexDataSize > 0) gpu.pImmediateContext->UpdateBuffer(gpu.pVBO, gTotalVertexBytes, vertexDataSize, mesh.vertices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (indexDataSize > 0) gpu.pImmediateContext->UpdateBuffer(gpu.pEBO, gTotalIndexBytes, indexDataSize, mesh.indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const MeshBounds bounds = ComputeBounds(mesh);
    const uint32_t meshId = static_cast<uint32_t>(gMeshInfos.size());
    gMeshInfos.push_back({.indexCount = static_cast<unsigned int>(mesh.indices.size()), .firstIndex = static_cast<unsigned int>(gTotalIndexBytes / sizeof(unsigned int)), .baseVertex = static_cast<unsigned int>(gTotalVertexBytes / (kVertexFloats * sizeof(float))), .boundingRadius = bounds.radius, .boundingCenter = glm::vec4(bounds.center, 1.0f)});

    gTotalVertexBytes += vertexDataSize;
    gTotalIndexBytes += indexDataSize;

    m_meshInfoDirty = true;
    return meshId;
}

glm::vec4 Renderer::getMeshBounds(uint32_t meshId) const {
    if (meshId >= gMeshInfos.size()) return glm::vec4(0.0f);
    const MeshInfo& info = gMeshInfos[meshId];
    return glm::vec4(glm::vec3(info.boundingCenter), info.boundingRadius);
}

void Renderer::storeNormalSamples(uint32_t baseId, const Mesh& mesh) {
    if (gNormalSamples.empty()) gNormalSamples.assign(static_cast<size_t>(kMaxMeshes) * kNormalSamples, glm::vec4(0.0f));
    const std::vector<float> samples = mesh.normalSamples(kNormalSamples);
    if (samples.size() < static_cast<size_t>(kNormalSamples) * 4) return;
    const float packedShape = PackedShape(mesh, gMeshInfos[baseId].boundingRadius);
    for (uint32_t sampleIndex = 0; sampleIndex < kNormalSamples; ++sampleIndex) {
        const size_t sourceOffset = static_cast<size_t>(sampleIndex) * 4;
        gNormalSamples[static_cast<size_t>(baseId) * kNormalSamples + sampleIndex] = glm::vec4(samples[sourceOffset], samples[sourceOffset + 1], samples[sourceOffset + 2], packedShape);
    }
}

uint32_t Renderer::uploadMesh(const Mesh& source) {
    if (source.vertices.empty() || source.indices.empty()) return 0;
    const Mesh optimizedMesh = source.optimized();
    return uploadProcessedMesh(optimizedMesh, optimizedMesh.generateLODChain());
}

uint32_t Renderer::uploadProcessedMesh(const Mesh& mesh, const std::vector<Mesh>& chain) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return 0;

    if (gMeshInfos.size() + kLodLevelCount > kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached ({} slots per mesh); refusing to upload another mesh.", kMaxMeshes, kLodLevelCount);
        return kInvalidMeshId;
    }

    const uint32_t baseId = uploadMeshSlot(mesh);
    if (baseId == kInvalidMeshId) return kInvalidMeshId;

    storeNormalSamples(baseId, mesh);

    const MeshInfo baseInfo = gMeshInfos[baseId];
    size_t previousIndexCount = mesh.indices.size();

    for (size_t level = 0; level < chain.size(); ++level) {
        const Mesh& lod = chain[level];
        const bool isImpostor = (level + 1 == chain.size());
        if (!isImpostor && (lod.indices.empty() || lod.indices.size() >= previousIndexCount)) {
            gMeshInfos.push_back(gMeshInfos.back());
            continue;
        }

        const uint32_t slot = uploadMeshSlot(lod);
        if (slot == kInvalidMeshId) break;
        gMeshInfos[slot].boundingRadius = baseInfo.boundingRadius;
        gMeshInfos[slot].boundingCenter = baseInfo.boundingCenter;
        previousIndexCount = lod.indices.size();
    }

    Lit::Log::Info("Mesh {} uploaded with {} LOD levels (index counts: {} -> {})", baseId, kLodLevelCount, mesh.indices.size(), gMeshInfos.back().indexCount);
    return baseId;
}

void Renderer::uploadBasePositions(const std::vector<glm::vec3>& basePositions) {
    if (basePositions.empty()) return;
    auto& gpu = *m_diligent;

    std::vector<glm::vec4> alignedPositions;
    alignedPositions.reserve(basePositions.size());
    for (const glm::vec3& position : basePositions) alignedPositions.emplace_back(position, 0.0f);

    gpu.pBasePositionBuffer = CreateStructuredBuffer(gpu.pDevice, "Base Position Buffer", sizeof(glm::vec4), static_cast<Diligent::Uint32>(alignedPositions.size()), alignedPositions.data());
    if (!gpu.pBasePositionBuffer) return;

    for (auto& transformBinding : gpu.pTransformSRB) {
        if (!transformBinding) continue;
        auto* variable = transformBinding->GetVariableByName(Diligent::SHADER_TYPE_COMPUTE, "BasePositionBuffer");
        if (variable) variable->Set(gpu.pBasePositionBuffer->GetDefaultView(Diligent::BUFFER_VIEW_SHADER_RESOURCE), Diligent::SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
    }
}
