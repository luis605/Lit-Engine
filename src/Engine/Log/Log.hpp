

#ifndef LIT_ENGINE_LOG_H
#define LIT_ENGINE_LOG_H

#include <string>
#include <string_view>
#include <source_location>
#include <format>

namespace Lit {

enum class LogLevel { Info,
                      Debug,
                      Warning,
                      Error,
                      Fatal };

class Log {
  public:
    static void Init();

    template <typename... Args>
    static void Info(std::format_string<Args...> message, Args&&... args) {

        LogInternal(LogLevel::Info, std::source_location::current(), std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Debug(std::format_string<Args...> message, Args&&... args) {
        LogInternal(LogLevel::Debug, std::source_location::current(), std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Warn(std::format_string<Args...> message, Args&&... args) {
        LogInternal(LogLevel::Warning, std::source_location::current(), std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Error(std::format_string<Args...> message, Args&&... args) {
        LogInternal(LogLevel::Error, std::source_location::current(), std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Fatal(std::format_string<Args...> message, Args&&... args) {
        LogInternal(LogLevel::Fatal, std::source_location::current(), std::format(message, std::forward<Args>(args)...));
    }

  private:
    static void LogInternal(LogLevel level, const std::source_location& location, const std::string& message);
};

} // namespace Lit

#endif