module;

#include <chrono>
#include <source_location>
#include <iostream>
#include <iomanip>

module Engine.Log;

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

    std::cout << '[' << std::setfill('0') << std::setw(2) << tm.tm_hour << ':'
              << std::setw(2) << tm.tm_min << ':' << std::setw(2) << tm.tm_sec << "] "
              << color << '[' << levelString << "] " << location.file_name() << ':'
              << location.line() << ':' << location.column() << ' ' << message << "\033[0m\n";

    if (level == LogLevel::Fatal) {
        exit(1);
    }
}
}