#include <atomic>

// Fallbacks for a DiligentCore without patches/diligent-upload-counters.patch; the patch's strong definitions override them.
extern "C" {
__attribute__((weak)) std::atomic<unsigned long long> g_LitUploadHeapBytes{0};
__attribute__((weak)) std::atomic<unsigned long long> g_LitDynamicHeapBytes{0};
}
