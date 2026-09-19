

#ifndef LIT_ENGINE_LOG_H
#define LIT_ENGINE_LOG_H

#include <string>
#include <string_view>
#include <source_location>
#include <format>
#include <concepts>
#include <type_traits>

namespace Lit {

enum class LogLevel { Info,
                      Debug,
                      Warning,
                      Error,
                      Fatal };

template <typename... Args>
struct FormatStringWithLocation {
    std::format_string<Args...> str;
    std::source_location loc;

    template <typename T>
    requires std::constructible_from<std::format_string<Args...>, const T&>
    consteval FormatStringWithLocation(const T& s, std::source_location l = std::source_location::current())
        : str(s), loc(l) {}
};

class Log {
  public:
    static void Init();

    template <typename... Args>
    static void Info(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Info, message.loc, std::format(message.str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Debug(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Debug, message.loc, std::format(message.str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Warn(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Warning, message.loc, std::format(message.str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Error(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Error, message.loc, std::format(message.str, std::forward<Args>(args)...));
    }

    template <typename... Args>
    static void Fatal(FormatStringWithLocation<std::type_identity_t<Args>...> message, Args&&... args) {
        LogInternal(LogLevel::Fatal, message.loc, std::format(message.str, std::forward<Args>(args)...));
    }

  private:
    static void LogInternal(LogLevel level, const std::source_location& location, const std::string& message);
};

} // namespace Lit

#endif