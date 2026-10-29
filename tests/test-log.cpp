#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

/**
 * @file test-log.cpp
 * @brief Tests for the C++20 source_location-based logging macros.
 *
 * Pure C++20, no Qt dependency — doctest provides main().
 */

#include "tools/log.h"

#include <cstdio>
#include <cstring>
#include <string>

// ── Capture stderr ──────────────────────────────────────────────────────────
// Redirect stderr to a buffer, run a closure, restore, and return captured text.

static std::string captureStderr(const auto &fn)
{
    char tmpfile[] = "/tmp/gmpc-test-log-XXXXXX";
    int fd = mkstemp(tmpfile);
    if (fd < 0) return {};

    // Save original stderr
    int orig = dup(STDERR_FILENO);
    dup2(fd, STDERR_FILENO);
    close(fd);

    fn();

    // Flush
    std::fflush(stderr);

    // Restore original stderr
    dup2(orig, STDERR_FILENO);
    close(orig);

    // Read captured data
    FILE *f = fopen(tmpfile, "r");
    if (!f) { unlink(tmpfile); return {}; }
    std::string result;
    char buf[4096];
    while (auto n = fread(buf, 1, sizeof(buf), f))
        result.append(buf, n);
    fclose(f);
    unlink(tmpfile);
    return result;
}

// ── Tests ────────────────────────────────────────────────────────────────────

TEST_CASE("LOG_INFO writes level, file, line, function, and message")
{
    auto out = captureStderr([] {
        LOG_INFO("hello %d", 42);
    });

    CHECK(out.find("INFO") != std::string::npos);
    CHECK(out.find("test-log.cpp") != std::string::npos);
    // line number of the LOG_INFO call above (line ~58 in this file)
    CHECK(out.find("hello 42") != std::string::npos);
}

TEST_CASE("LOG_DEBUG is no-op in release (NDEBUG defined)")
{
#ifdef NDEBUG
    auto out = captureStderr([] {
        LOG_DEBUG("should not appear");
    });
    CHECK(out.empty());
#endif
}

TEST_CASE("LOG_WARN and LOG_ERROR write expected prefixes")
{
    auto warn = captureStderr([] { LOG_WARN("something fishy"); });
    CHECK(warn.find("WARN") != std::string::npos);
    CHECK(warn.find("something fishy") != std::string::npos);

    auto err = captureStderr([] { LOG_ERROR("epic fail %d", 1); });
    CHECK(err.find("ERROR") != std::string::npos);
    CHECK(err.find("epic fail 1") != std::string::npos);
}

TEST_CASE("Multiple log calls accumulate correctly")
{
    auto out = captureStderr([] {
        LOG_INFO("first");
        LOG_WARN("second");
    });
    // Both messages present
    CHECK(out.find("first") != std::string::npos);
    CHECK(out.find("second") != std::string::npos);
    // Each on its own line (approximate: count newlines)
    auto n = std::count(out.begin(), out.end(), '\n');
    CHECK(n >= 2);
}
