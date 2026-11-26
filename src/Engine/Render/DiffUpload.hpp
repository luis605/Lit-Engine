#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "DiligentCore/Graphics/GraphicsEngine/interface/Buffer.h"
#include "DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h"
#include "UploadStats.hpp"

namespace litgpu {

inline bool legacyUploads() {
    static const bool legacy = [] {
        const char* value = std::getenv("LIT_UPLOAD_LEGACY");
        return value && *value && *value != '0';
    }();
    return legacy;
}

class DiffUploader {
public:
    static constexpr Diligent::Uint64 kBlock = 256;
    static constexpr Diligent::Uint64 kMergeGapBlocks = 16;

    static DiffUploader& get() {
        static DiffUploader uploader;
        return uploader;
    }

    void update(Diligent::IDeviceContext* context, Diligent::IBuffer* buffer, Diligent::Uint64 offset, Diligent::Uint64 size, const void* data, const char* file, int line) {
        if (size == 0) return;
        if (legacyUploads()) {
            Lit::UploadStats::Counters before = Lit::UploadStats::snapshot();
            context->UpdateBuffer(buffer, offset, size, data, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Lit::UploadStats::Registry::get().attribute(file, line, before);
            return;
        }
        Entry& entry = m_entries[buffer->GetUniqueID()];
        const Diligent::Uint64 bufferSize = buffer->GetDesc().Size;
        if (entry.size != bufferSize) {
            entry.size = bufferSize;
            entry.shadow.assign(bufferSize, 0);
            entry.valid.assign((bufferSize + kBlock - 1) / kBlock, 0);
        }
        const auto* source = static_cast<const uint8_t*>(data);
        const Diligent::Uint64 end = offset + size;
        const Diligent::Uint64 firstBlock = offset / kBlock;
        const Diligent::Uint64 lastBlock = (end - 1) / kBlock;
        Diligent::Uint64 runStart = ~0ull;
        Diligent::Uint64 runEnd = 0;
        Diligent::Uint64 cleanSince = 0;
        auto flush = [&] {
            if (runStart == ~0ull) return;
            Lit::UploadStats::Counters before = Lit::UploadStats::snapshot();
            context->UpdateBuffer(buffer, runStart, runEnd - runStart, source + (runStart - offset), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            Lit::UploadStats::Registry::get().attribute(file, line, before);
            runStart = ~0ull;
        };
        for (Diligent::Uint64 block = firstBlock; block <= lastBlock; ++block) {
            const Diligent::Uint64 begin = std::max(block * kBlock, offset);
            const Diligent::Uint64 stop = std::min((block + 1) * kBlock, end);
            const bool full = begin == block * kBlock && (stop == (block + 1) * kBlock || stop == bufferSize);
            const bool same = entry.valid[block] && std::memcmp(entry.shadow.data() + begin, source + (begin - offset), stop - begin) == 0;
            if (same) {
                if (runStart != ~0ull && block - cleanSince >= kMergeGapBlocks) flush();
                continue;
            }
            std::memcpy(entry.shadow.data() + begin, source + (begin - offset), stop - begin);
            if (full) entry.valid[block] = 1;
            if (runStart == ~0ull) runStart = begin;
            runEnd = stop;
            cleanSince = block + 1;
        }
        flush();
    }

    void invalidate(const Diligent::IBuffer* buffer) {
        if (auto found = m_entries.find(buffer->GetUniqueID()); found != m_entries.end()) std::ranges::fill(found->second.valid, 0);
    }

private:
    struct Entry {
        Diligent::Uint64 size = 0;
        std::vector<uint8_t> shadow;
        std::vector<uint8_t> valid;
    };
    std::unordered_map<Diligent::Int32, Entry> m_entries;
};

}

namespace litgpu {

inline void zeroFill(Diligent::IDeviceContext* context, Diligent::IBuffer* zero, Diligent::IBuffer* destination, Diligent::Uint64 offset, Diligent::Uint64 bytes, const void* fallbackZeros) {
    if (bytes == 0) return;
    if (!legacyUploads() && zero && zero->GetDesc().Size >= bytes) {
        context->CopyBuffer(zero, 0, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION, destination, offset, bytes, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        return;
    }
    LIT_UP(context)->UpdateBuffer(destination, offset, bytes, fallbackZeros, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

}

#define LIT_UPDATE_DIFF(context, buffer, offset, size, data) ::litgpu::DiffUploader::get().update(::Lit::UploadStats::rawPointer(context), ::Lit::UploadStats::rawPointer(buffer), (offset), (size), (data), __FILE__, __LINE__)
