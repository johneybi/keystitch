/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "base/MacInputSourceBackend.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace inputleap {

// Local engine-to-parent status records. The per-launch tag keeps normal
// diagnostic lines and records from previous children out of the state model.
enum class PeerStatus { Unknown, Connecting, Listening, Connected, ConnectedEncrypted, Disconnected };
inline constexpr char peerStatusEnv[] = "KEYSTITCH_STATUS_TAG";
inline constexpr char peerStatusPrefix[] = "KeyStitch-peer-v1 ";
struct PeerStatusRecord { PeerStatus state; const char* name; };
inline constexpr std::array<PeerStatusRecord, 5> peerStatusRecords{{
    {PeerStatus::Connecting, "connecting"}, {PeerStatus::Listening, "listening"},
    {PeerStatus::Connected, "connected"}, {PeerStatus::ConnectedEncrypted, "connected-encrypted"},
    {PeerStatus::Disconnected, "disconnected"}
}};

inline void writeEngineStatus(const char* field, const char* value)
{
    const char* tag = std::getenv(peerStatusEnv);
    if (!tag) return;
    const std::string_view id(tag);
    if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != id.npos) return;
    std::fprintf(stdout, "%s%s %s%s\n", peerStatusPrefix, tag, field, value);
    std::fflush(stdout);
}

inline void reportMacInputSourceBackend(std::optional<MacInputSourceBackend> backend)
{
    writeEngineStatus("ime ", backend ? macInputSourceBackendName(*backend) : "inactive");
}

inline void reportPeerStatus(PeerStatus status)
{
    for (const auto& record : peerStatusRecords) {
        if (record.state == status) {
            writeEngineStatus("", record.name);
            return;
        }
    }
}

} // namespace inputleap
