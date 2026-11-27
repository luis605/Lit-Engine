#include <atomic>

extern "C" {
__attribute__((weak)) std::atomic<unsigned long long> g_LitUploadHeapBytes{0};
__attribute__((weak)) std::atomic<unsigned long long> g_LitDynamicHeapBytes{0};
}
