/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <optional>
#include <string>

namespace inputleap {

// Platform-independent configuration contract; this header performs no OS calls.
enum class MacInputSourceBackend { DirectTIS, NativeShortcut, Invalid };

inline MacInputSourceBackend parseMacInputSourceBackend(const std::string& value)
{
    if (value == "direct-tis") return MacInputSourceBackend::DirectTIS;
    if (value == "native-shortcut") return MacInputSourceBackend::NativeShortcut;
    return MacInputSourceBackend::Invalid;
}

inline const char* macInputSourceBackendName(MacInputSourceBackend value)
{
    switch (value) {
    case MacInputSourceBackend::DirectTIS: return "direct-tis";
    case MacInputSourceBackend::NativeShortcut: return "native-shortcut";
    default: return "invalid";
    }
}

struct MacInputSourceSelection {
    MacInputSourceBackend backend;
    const char* source;
};

inline MacInputSourceSelection resolveMacInputSourceBackend(
    std::optional<MacInputSourceBackend> explicitBackend, const char* environment)
{
    if (explicitBackend) return {*explicitBackend, "command-line"};
    if (environment && *environment) {
        return {parseMacInputSourceBackend(environment), "environment"};
    }
    // Preserve the standalone engine's legacy default. The product launcher
    // supplies its persisted choice explicitly, including its native default.
    return {MacInputSourceBackend::DirectTIS, "legacy-default"};
}

} // namespace inputleap
