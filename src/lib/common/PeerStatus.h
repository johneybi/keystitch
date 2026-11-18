/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

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

inline void reportPeerStatus(PeerStatus status)
{
    const char* tag = std::getenv(peerStatusEnv);
    if (!tag) return;
    const std::string_view id(tag);
    if (id.size() != 32 || id.find_first_not_of("0123456789abcdef") != id.npos) return;
    for (const auto& record : peerStatusRecords) {
        if (record.state == status) {
            std::fprintf(stdout, "%s%s %s\n", peerStatusPrefix, tag, record.name);
            std::fflush(stdout);
            return;
        }
    }
}

} // namespace inputleap
