#pragma once

/**
 * @file log.h
 * @brief Logging macros with std::source_location (C++20).
 *
 * All macros accept a printf-style format string and arguments.
 * File, line, and function are captured automatically.
 * LOG_DEBUG is a no-op in release builds.
 */

#include <cstdio>
#include <cstdarg>
#include <source_location>

///@cond internal
namespace detail {

/// Maximum log-message length.
inline constexpr std::size_t kLogBufSize = 4096;

/// Write a printf-style log line to stderr with meta-data.
inline void logPrintf(const std::source_location &loc,
                      const char *level,
                      const char *fmt,
                      ...)
{
    char buf[kLogBufSize];
    // 1) write header: "LEVEL file:line (func) "
    int hdr = std::snprintf(buf, sizeof(buf),
                            "%s %s:%u (%s) ",
                            level,
                            loc.file_name(), loc.line(),
                            loc.function_name());

    // 2) append user message
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf + hdr, sizeof(buf) - hdr, fmt, ap);
    va_end(ap);

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

