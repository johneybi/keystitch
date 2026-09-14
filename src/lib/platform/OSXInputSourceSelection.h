/*
 * InputLeap -- mouse and keyboard sharing utility
 * Copyright (C) 2012-2016 Symless Ltd.
 * Copyright (C) 2004 Chris Schoeneman
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace inputleap {

// Pure (OS-effect-free) macOS input-source selection policy.
//
// The helper never touches TIS, CFPreferences, sleep, or events. It only
// inspects the candidate list, the current ID, the read-only Gureum
// preference IDs, and the requested offset, then returns a target ID plus a
// diagnosable reason string. All OS interaction stays in OSXKeyState.

struct OSXInputSourceCandidate {
    std::string m_id;
    bool m_enabled = false;
    bool m_selectable = false;
};

struct OSXInputSourceSelection {
    std::string m_targetId; // empty when no enabled+selectable candidate exists
    std::string m_reason;   // diagnosable, never empty
};

// Exact IDs (no arbitrary-prefix matching). Gureum IDs are used to validate
// preferences; the Apple IDs are the historical fallback/classification IDs.
extern const char* const kOSXAppleRomanABC;
extern const char* const kOSXAppleKorean;

// Classification helpers (exact-match only).
bool OSXInputSourceIsGureumRoman(const std::string& id);
bool OSXInputSourceIsGureumHangul(const std::string& id);
bool OSXInputSourceIsRoman(const std::string& id);     // Gureum Roman(4) + Apple ABC
bool OSXInputSourceIsHangul(const std::string& id);    // Gureum Hangul set + Apple Korean
bool OSXInputSourceIsRegistered(const std::string& id); // Roman or Hangul

OSXInputSourceSelection PickOSXInputSourceTarget(
    const std::vector<OSXInputSourceCandidate>& candidates,
    const std::string& currentId,
    const std::string& preferredRomanId,
    const std::string& preferredHangulId,
    std::int32_t offset);

} // namespace inputleap
