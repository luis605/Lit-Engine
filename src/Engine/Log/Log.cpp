#include "Engine/Log/Log.hpp"
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace Lit {

namespace {
const auto kProcessStart = std::chrono::steady_clock::now();
auto lastPhase = kProcessStart;
}

void Log::Init() {}

void Log::Phase(std::string_view name, std::source_location location) {
    const auto now = std::chrono::steady_clock::now();
    const double stepMilliseconds = std::chrono::duration<double, std::milli>(now - lastPhase).count();
    const double totalMilliseconds = std::chrono::duration<double, std::milli>(now - kProcessStart).count();
    lastPhase = now;
    LogInternal(LogLevel::Info, location, std::format("[startup] {}: {:.1f} ms (at {:.1f} ms)", name, stepMilliseconds, totalMilliseconds));
}

void Log::LogInternal(LogLevel level, const std::source_location& location, const std::string& message) {
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime{};
    localtime_r(&time, &localTime);

    const char* levelString = "INFO";
    const char* color = "\033[32m";

    switch (level) {
    case LogLevel::Info:
        levelString = "INFO";
        color = "\033[32m";
        break;
    case LogLevel::Debug:
        levelString = "DEBUG";
        color = "\033[34m";
        break;
    case LogLevel::Warning:
        levelString = "WARNING";
        color = "\033[33m";
        break;
    case LogLevel::Error:
        levelString = "ERROR";
        color = "\033[31m";
        break;
    case LogLevel::Fatal:
        levelString = "FATAL";
        color = "\033[31;1m";
        break;
    }

    std::cout << '[' << std::setfill('0') << std::setw(2) << localTime.tm_hour << ':'
              << std::setw(2) << localTime.tm_min << ':' << std::setw(2) << localTime.tm_sec << "] "
              << color << '[' << levelString << "] " << location.file_name() << ':'
              << location.line() << ' ' << message << "\033[0m"
              << std::endl;

    if (level == LogLevel::Fatal) {
        exit(1);
    }
}

}