#pragma once

/**
 * @file log.h
 * @brief Logging macros with std::source_location (C++20).
 *
 * All macros accept a std::format-style format string and arguments.
 * File, line, and function are captured automatically.
 * LOG_DEBUG is a no-op in release builds.
 *
 * QString is accepted directly — no .toUtf8().constData() needed.
 */

#include <cstdio>
#include <source_location>
#include <format>
#include <string>
#include <QString>

// ── Formatter so std::format can print QString ────────────────────────────
template <>
struct std::formatter<QString, char> : std::formatter<std::string, char> {
    auto format(const QString &s, auto &ctx) const {
        return std::formatter<std::string, char>::format(s.toStdString(), ctx);
    }
};

///@cond internal
namespace detail {

/// Maximum log-message length.
inline constexpr std::size_t kLogBufSize = 4096;

/// Write a {}-format log line to stderr with meta-data.
template <typename... Args>
inline void logPrintf(const std::source_location &loc,
                      const char *level,
                      std::format_string<Args...> fmt,
                      Args &&...args)
{
    char buf[kLogBufSize];
    int hdr = std::snprintf(buf, sizeof(buf),
                            "%s %s:%u (%s) ",
                            level,
                            loc.file_name(), loc.line(),
                            loc.function_name());

    std::string body = std::format(fmt, std::forward<Args>(args)...);
    std::snprintf(buf + hdr, sizeof(buf) - hdr, "%s", body.c_str());

    buf[sizeof(buf) - 1] = '\0';
    std::fputs(buf, stderr);
    std::fputc('\n', stderr);
}

} // namespace detail
///@endcond

// -------------------------------------------------------------------
// Public macros
// -------------------------------------------------------------------

/// Debug-level log (compiled out in release builds).
#ifdef NDEBUG
#  define LOG_DEBUG(...) ((void)0)
#else
#  define LOG_DEBUG(fmt, ...)                                           \
    do {                                                                \
        ::detail::logPrintf(::std::source_location::current(),          \
                            "DEBUG", (fmt) __VA_OPT__(, )              \
                            ##__VA_ARGS__);                             \
    } while (0)
#endif // NDEBUG

/// Informational log.
#define LOG_INFO(fmt, ...)                                              \
    do {                                                                \
        ::detail::logPrintf(::std::source_location::current(),          \
                            "INFO", (fmt) __VA_OPT__(, )               \
                            ##__VA_ARGS__);                             \
    } while (0)

/// Warning log.
#define LOG_WARN(fmt, ...)                                              \
    do {                                                                \
        ::detail::logPrintf(::std::source_location::current(),          \
                            "WARN", (fmt) __VA_OPT__(, )               \
                            ##__VA_ARGS__);                             \
    } while (0)

/// Error log.
#define LOG_ERROR(fmt, ...)                                              \
    do {                                                                 \
        ::detail::logPrintf(::std::source_location::current(),           \
                            "ERROR", (fmt) __VA_OPT__(, )               \
                            ##__VA_ARGS__);                              \
    } while (0)
