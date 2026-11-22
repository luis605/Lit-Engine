module;

#include <unordered_set>
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

// Geometry released by Renderer::releaseMesh: ranges of the shared vertex and index buffers plus whole LOD blocks of
// mesh slots, reused by later uploads (terrain tiles stream in and out, every tile has the same size).
struct GeometryRange {
    size_t vertexOffset = 0;
    size_t vertexBytes = 0;
    size_t indexOffset = 0;
    size_t indexBytes = 0;
};
std::vector<GeometryRange> gFreeRanges;
std::unordered_map<uint32_t, GeometryRange> gSlotRanges;
std::vector<uint32_t> gFreeBlocks;
std::unordered_set<uint32_t> gLiveBlocks;

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

uint32_t Renderer::uploadMeshSlot(const Mesh& mesh, uint32_t forcedSlot) {
    auto& gpu = *m_diligent;
    if (forcedSlot == 0xFFFFFFFFu && gMeshInfos.size() >= kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached; refusing to upload another mesh.", kMaxMeshes);
        return kInvalidMeshId;
    }

    const size_t vertexDataSize = mesh.vertices.size() * sizeof(float);
    const size_t indexDataSize = mesh.indices.size() * sizeof(unsigned int);

    // Best fit among released ranges, otherwise append to the buffers.
    GeometryRange range;
    size_t best = gFreeRanges.size();
    for (size_t i = 0; i < gFreeRanges.size(); ++i) {
        const GeometryRange& candidate = gFreeRanges[i];
        if (candidate.vertexBytes < vertexDataSize || candidate.indexBytes < indexDataSize) continue;
        if (best == gFreeRanges.size() || candidate.vertexBytes + candidate.indexBytes < gFreeRanges[best].vertexBytes + gFreeRanges[best].indexBytes) best = i;
    }
    if (best < gFreeRanges.size()) {
        range = gFreeRanges[best];
        gFreeRanges.erase(gFreeRanges.begin() + static_cast<std::ptrdiff_t>(best));
    } else {
        growGeometryBuffers(vertexDataSize, indexDataSize);
        range = {gTotalVertexBytes, vertexDataSize, gTotalIndexBytes, indexDataSize};
        gTotalVertexBytes += vertexDataSize;
        gTotalIndexBytes += indexDataSize;
    }
    if (vertexDataSize > 0) gpu.pImmediateContext->UpdateBuffer(gpu.pVBO, range.vertexOffset, vertexDataSize, mesh.vertices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if (indexDataSize > 0) gpu.pImmediateContext->UpdateBuffer(gpu.pEBO, range.indexOffset, indexDataSize, mesh.indices.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    const MeshBounds bounds = ComputeBounds(mesh);
    const MeshInfo info{.indexCount = static_cast<unsigned int>(mesh.indices.size()), .firstIndex = static_cast<unsigned int>(range.indexOffset / sizeof(unsigned int)), .baseVertex = static_cast<unsigned int>(range.vertexOffset / (kVertexFloats * sizeof(float))), .boundingRadius = bounds.radius, .boundingCenter = glm::vec4(bounds.center, 1.0f)};
    uint32_t meshId = forcedSlot;
    if (forcedSlot == 0xFFFFFFFFu) {
        meshId = static_cast<uint32_t>(gMeshInfos.size());
        gMeshInfos.push_back(info);
    } else {
        gMeshInfos[forcedSlot] = info;
    }
    gSlotRanges[meshId] = range;

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

    // A released block of kLodLevelCount slots is reused before the slot table grows.
    uint32_t block = kInvalidMeshId;
    if (!gFreeBlocks.empty()) {
        block = gFreeBlocks.back();
        gFreeBlocks.pop_back();
    } else if (gMeshInfos.size() + kLodLevelCount > kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached ({} slots per mesh); refusing to upload another mesh.", kMaxMeshes, kLodLevelCount);
        return kInvalidMeshId;
    }
    size_t cursor = 0;
    const auto nextSlot = [&]() { return block == kInvalidMeshId ? kInvalidMeshId : block + static_cast<uint32_t>(cursor++); };
    const auto duplicatePrevious = [&]() {
        if (block == kInvalidMeshId) {
            gMeshInfos.push_back(gMeshInfos.back());
        } else {
            gMeshInfos[block + cursor] = gMeshInfos[block + cursor - 1];
            ++cursor;
        }
    };

    const uint32_t baseId = uploadMeshSlot(mesh, nextSlot());
    if (baseId == kInvalidMeshId) return kInvalidMeshId;
    gLiveBlocks.insert(baseId);

    storeNormalSamples(baseId, mesh);

    const MeshInfo baseInfo = gMeshInfos[baseId];
    size_t previousIndexCount = mesh.indices.size();

    for (size_t level = 0; level < chain.size(); ++level) {
        const Mesh& lod = chain[level];
        const bool isImpostor = (level + 1 == chain.size());
        if (!isImpostor && (lod.indices.empty() || lod.indices.size() >= previousIndexCount)) {
            duplicatePrevious();
            continue;
        }

        const uint32_t slot = uploadMeshSlot(lod, nextSlot());
        if (slot == kInvalidMeshId) break;
        gMeshInfos[slot].boundingRadius = baseInfo.boundingRadius;
        gMeshInfos[slot].boundingCenter = baseInfo.boundingCenter;
        previousIndexCount = lod.indices.size();
    }

    Lit::Log::Info("Mesh {} uploaded with {} LOD levels (index counts: {} -> {})", baseId, kLodLevelCount, mesh.indices.size(), gMeshInfos[baseId + std::min<size_t>(chain.size(), kLodLevelCount - 1)].indexCount);
    return baseId;
}

uint32_t Renderer::uploadTerrainMesh(const Mesh& mesh) {
    if (mesh.vertices.empty() || mesh.indices.empty()) return 0;
    // Tiles are morphed in the vertex shader, so no simplification and no point impostor: every LOD slot shows the base geometry.
    uint32_t block = kInvalidMeshId;
    if (!gFreeBlocks.empty()) {
        block = gFreeBlocks.back();
        gFreeBlocks.pop_back();
    } else if (gMeshInfos.size() + kLodLevelCount > kMaxMeshes) {
        Lit::Log::Error("Mesh slot limit of {} reached; refusing to upload terrain tile.", kMaxMeshes);
        return kInvalidMeshId;
    }
    const uint32_t baseId = uploadMeshSlot(mesh, block);
    if (baseId == kInvalidMeshId) return kInvalidMeshId;
    gLiveBlocks.insert(baseId);
    storeNormalSamples(baseId, mesh);
    for (size_t level = 1; level < kLodLevelCount; ++level) {
        if (block == kInvalidMeshId) gMeshInfos.push_back(gMeshInfos[baseId]);
        else gMeshInfos[baseId + level] = gMeshInfos[baseId];
    }
    return baseId;
}

void Renderer::releaseMesh(uint32_t baseId) {
    if (!gLiveBlocks.erase(baseId)) return;
    releaseClusterMesh(baseId);
    for (uint32_t level = 0; level < kLodLevelCount; ++level) {
        const uint32_t slot = baseId + level;
        if (slot >= gMeshInfos.size()) break;
        if (const auto it = gSlotRanges.find(slot); it != gSlotRanges.end()) {
            gFreeRanges.push_back(it->second);
            gSlotRanges.erase(it);
        }
        gMeshInfos[slot].indexCount = 0;
        gMeshInfos[slot].boundingRadius = 0.0f;
    }
    if (!gNormalSamples.empty() && static_cast<size_t>(baseId + 1) * kNormalSamples <= gNormalSamples.size()) {
        std::fill_n(gNormalSamples.begin() + static_cast<std::ptrdiff_t>(static_cast<size_t>(baseId) * kNormalSamples), kNormalSamples, glm::vec4(0.0f));
    }
    gFreeBlocks.push_back(baseId);
    m_meshInfoDirty = true;
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
