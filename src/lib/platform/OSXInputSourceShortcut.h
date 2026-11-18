/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include "base/MacInputSourceBackend.h"
#include <CoreFoundation/CoreFoundation.h>
#include <cstdint>
#include <string>
#include <vector>

namespace inputleap {

using OSXInputSourceBackend = MacInputSourceBackend;
OSXInputSourceBackend ParseOSXInputSourceBackend(const std::string& value);
const char* OSXInputSourceBackendName(OSXInputSourceBackend backend);

struct OSXInputSourceShortcut {
    std::uint16_t keyCode = 0;
    std::uint64_t modifiers = 0; // Quartz flags, not Carbon modifier bits.
};

struct OSXInputSourceShortcutResult {
    bool valid = false;
    OSXInputSourceShortcut shortcut;
    std::string reason;
};

// Data-only helpers: no preference reads, TIS, event creation/posting or waits.
// The symbolic-hotkey preference schema is undocumented; reject missing or
// changed schemas rather than guessing a default shortcut or writing settings.
OSXInputSourceShortcutResult ParseOSXInputSourceShortcut(CFPropertyListRef preferences);
bool OSXInputSourceShortcutIsRegistered(const OSXInputSourceShortcut& shortcut,
                                        CFArrayRef symbolicHotKeys);

struct OSXInputSourceShortcutEvent {
    std::uint16_t keyCode;
    bool down;
    std::uint64_t flags;
};

// Empty on unsupported shortcuts or held synthetic modifiers. Events balance
// modifiers without modifying OSXKeyState's held-key bookkeeping.
std::vector<OSXInputSourceShortcutEvent> MakeOSXInputSourceShortcutEvents(
    const OSXInputSourceShortcut& shortcut, std::uint64_t heldModifiers);

} // namespace inputleap
