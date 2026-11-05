#pragma once

#include <cstdio>
#include <filesystem>
#include <string>

namespace testsupport {

inline int checkCount = 0;
inline int failureCount = 0;

inline void recordCheck(bool passed, const char* expression, int line) {
    ++checkCount;
    if (passed) return;
    ++failureCount;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
}

inline void recordStatus(const std::string& error, const char* expression, int line) {
    ++checkCount;
    if (error.empty()) return;
    ++failureCount;
    std::fprintf(stderr, "FAIL line %d: %s -> %s\n", line, expression, error.c_str());
}

inline std::string tempFilePath(const std::string& prefix, const std::string& name) {
    return (std::filesystem::temp_directory_path() / (prefix + name)).string();
}

inline int finish() {
    std::printf("%d checks, %d failures\n", checkCount, failureCount);
    return failureCount == 0 ? 0 : 1;
}

}

#define CHECK(expression) ::testsupport::recordCheck(static_cast<bool>(expression), #expression, __LINE__)
#define CHECK_OK(expression) ::testsupport::recordStatus((expression), #expression, __LINE__)
