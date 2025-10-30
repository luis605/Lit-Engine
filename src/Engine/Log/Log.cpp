module;

#include <iostream>
#include <chrono>
#include <format>
#include <source_location>

module Log;

namespace Lit {
void Log::Init() {}

void Log::Print(LogLevel level, const std::string& message, const std::source_location& location) {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::localtime(&time);

    const char* levelString;
    const char* color;

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

    std::cout << std::format("[{:02d}:{:02d}:{:02d}] {}[{}] {}:{}:{} {}\033[0m", tm.tm_hour, tm.tm_min, tm.tm_sec, color, levelString, location.file_name(),
                             location.line(), location.column(), message)
              << std::endl;

    if (level == LogLevel::Fatal) {
        exit(1);
    }
}
} 