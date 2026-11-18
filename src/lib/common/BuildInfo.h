/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <optional>
#include <string>

namespace inputleap {

enum class BuildComponent { Gui, Client, Server, Daemon };

std::string buildInfoJson(BuildComponent component);

// Call before QApplication, Arch, settings, locks, service or input setup.
// A present option is always handled, including invalid extra arguments.
std::optional<int> handleBuildInfoRequest(int argc, const char* const* argv,
                                        BuildComponent component);

} // namespace inputleap
