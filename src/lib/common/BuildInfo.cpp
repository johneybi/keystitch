/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "BuildInfo.h"
#include "BuildInfoGenerated.h"

#include <cstdio>
#include <cstring>

namespace inputleap {

std::string buildInfoJson(BuildComponent component)
{
    const char* name = "unknown";
    switch (component) {
    case BuildComponent::Gui: name = "gui"; break;
    case BuildComponent::Client: name = "client"; break;
    case BuildComponent::Server: name = "server"; break;
    case BuildComponent::Daemon: name = "daemon"; break;
    }
    std::string json = KEYSTITCH_BUILD_METADATA;
    json.pop_back();
    json += ",\"configuration\":\"" KEYSTITCH_BUILD_CONFIGURATION "\",\"component\":\"";
    json += name;
    json += "\"}";
    return json;
}

std::optional<int> handleBuildInfoRequest(int argc, const char* const* argv,
                                        BuildComponent component)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--build-info") != 0) continue;
        if (argc != 2) {
            std::fputs("--build-info must be used alone\n", stderr);
            return 2;
        }
        const auto json = buildInfoJson(component);
        std::puts(json.c_str());
        return 0;
    }
    return std::nullopt;
}

} // namespace inputleap
