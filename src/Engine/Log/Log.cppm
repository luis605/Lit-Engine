module;

#include <iostream>
#include <string>
#include <source_location>
#include <chrono>
#include <format>
#include <memory>

export module Log;

export namespace Lit {
enum class LogLevel { Info, Debug, Warning, Error, Fatal };

class Log {
  public:
    static void Init();

    static void Print(LogLevel level, const std::string& message, const std::source_location& location = std::source_location::current());

    template <typename... Args> static void Info(const std::string& message, Args&&... args) {
        std::string formattedMessage = Format(message, std::forward<Args>(args)...);
        Print(LogLevel::Info, formattedMessage);
    }

    template <typename... Args> static void Debug(const std::string& message, Args&&... args) {
        std::string formattedMessage = Format(message, std::forward<Args>(args)...);
        Print(LogLevel::Debug, formattedMessage);
    }

    template <typename... Args> static void Warn(const std::string& message, Args&&... args) {
        std::string formattedMessage = Format(message, std::forward<Args>(args)...);
        Print(LogLevel::Warning, formattedMessage);
    }

    template <typename... Args> static void Error(const std::string& message, Args&&... args) {
        std::string formattedMessage = Format(message, std::forward<Args>(args)...);
        Print(LogLevel::Error, formattedMessage);
    }

    template <typename... Args> static void Fatal(const std::string& message, Args&&... args) {
        std::string formattedMessage = Format(message, std::forward<Args>(args)...);
        Print(LogLevel::Fatal, formattedMessage);
    }

  private:
    template <typename... Args> static std::string Format(const std::string& format, Args&&... args) {
        size_t size = snprintf(nullptr, 0, format.c_str(), args...) + 1;
        std::unique_ptr<char[]> buf(new char[size]);
        snprintf(buf.get(), size, format.c_str(), args...);
        return std::string(buf.get(), buf.get() + size - 1);
    }
};
} // namespace Lit
