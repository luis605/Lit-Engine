#include <cstddef>
#include <cstdlib>

extern "C" {
    void* python_hashlib_Hacl_Hash_SHA2_malloc_512() { return nullptr; }
    void python_hashlib_Hacl_Hash_SHA2_free_512(void* ptr) {}
    void python_hashlib_Hacl_Hash_SHA2_update_512(void*, const void*, size_t) {}
    void python_hashlib_Hacl_Hash_SHA2_digest_512(void*, void*) {}
    void python_hashlib_Hacl_Hash_SHA2_copy_512(void*, void*) {}
    void* python_hashlib_Hacl_Hash_SHA2_malloc_256() { return nullptr; }
    void python_hashlib_Hacl_Hash_SHA2_free_256(void* ptr) {}
    void python_hashlib_Hacl_Hash_SHA2_update_256(void*, const void*, size_t) {}
    void python_hashlib_Hacl_Hash_SHA2_digest_256(void*, void*) {}
    void python_hashlib_Hacl_Hash_SHA2_copy_256(void*, void*) {}
    void* python_hashlib_Hacl_Hash_SHA2_malloc_224() { return nullptr; }
    void* python_hashlib_Hacl_Hash_SHA2_malloc_384() { return nullptr; }
}