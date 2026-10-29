/**
 * @file trompeloeil-doctest.hpp
 * @brief Trompeloeil adapter for doctest.
 *
 * Include this instead of <trompeloeil.hpp> when using doctest.
 * Must be included AFTER <doctest/doctest.h>.
 */

#pragma once

#include <trompeloeil.hpp>

namespace trompeloeil {

template <>
inline void reporter<specialized>::send(
    severity s,
    const char *file,
    unsigned long line,
    const char *msg)
{
    std::ostringstream os;
    if (line)
        os << file << ':' << line << '\n';
    os << msg;
    auto failure = os.str();
    if (s == severity::fatal)
        FAIL(failure);
    else
        CHECK_MESSAGE(false, failure);
}

template <>
inline void reporter<specialized>::sendOk(
    const char * /*trompeloeil_mock_calls_done_correctly*/)
{
    CHECK_MESSAGE(true, "");
}

} // namespace trompeloeil

