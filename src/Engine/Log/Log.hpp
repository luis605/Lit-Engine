#ifndef LIT_ENGINE_LOG_H
#define LIT_ENGINE_LOG_H

#include <string>
#include <string_view>
#include <utility>
#include <source_location>
#include <format>
#include <concepts>
#include <type_traits>

namespace Lit {

enum class LogLevel { Info, Debug, Warning, Error, Fatal };

template <typename... Args>
struct FormatStringWithLocation {
    std::format_string<Args...> format;
    std::source_location location;

    template <typename T>
        requires std::constructible_from<std::format_string<Args...>, const T&>
    consteval FormatStringWithLocation(const T& text, std::source_location sourceLocation = std::source_location::current()) : format(text), location(sourceLocation) {}
};

class Log {
   public:
    static void Init();

    template <typename... Args>
    static void Info(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Info, message.location, std::format(message.format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Debug(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Debug, message.location, std::format(message.format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Warn(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Warning, message.location, std::format(message.format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Error(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Error, message.location, std::format(message.format, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Fatal(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Fatal, message.location, std::format(message.format, std::forward<Args>(args)...));
    }

    static void Phase(std::string_view name, std::source_location location = std::source_location::current());

   private:
    static void LogInternal(LogLevel level, const std::source_location& location, const std::string& message);
};

}

#endif
