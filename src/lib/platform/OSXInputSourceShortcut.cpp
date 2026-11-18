/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "platform/OSXInputSourceShortcut.h"

#include <Carbon/Carbon.h>

namespace inputleap {
namespace {

bool integer(CFTypeRef value, std::int64_t& out)
{
    return value != nullptr && CFGetTypeID(value) == CFNumberGetTypeID()
        && !CFNumberIsFloatType(static_cast<CFNumberRef>(value))
        && CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberSInt64Type, &out);
}

CFTypeRef field(CFTypeRef value, CFStringRef key)
{
    if (value == nullptr || CFGetTypeID(value) != CFDictionaryGetTypeID()) {
        return nullptr;
    }
    return CFDictionaryGetValue(static_cast<CFDictionaryRef>(value), key);
}

bool functionKey(std::uint16_t key)
{
    for (int candidate : {kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7,
            kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15,
            kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20}) {
        if (key == candidate) return true;
    }
    return false;
}

bool supported(const OSXInputSourceShortcut& shortcut)
{
    const auto allowed = kCGEventFlagMaskControl | kCGEventFlagMaskAlternate
        | kCGEventFlagMaskShift | kCGEventFlagMaskCommand
        | (functionKey(shortcut.keyCode) ? kCGEventFlagMaskSecondaryFn : 0);
    if (shortcut.modifiers & ~static_cast<std::uint64_t>(allowed)) return false;
    if (functionKey(shortcut.keyCode)) return true;
    // Deliberately narrow experimental scope: reject text-generating keys,
    // bare/Shift-Space, modifier/lock keys and general Fn/Globe shortcuts.
    // F1-F20 may carry the function flag even when Settings shows only F19.
    return shortcut.keyCode == kVK_Space && (shortcut.modifiers
        & (kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand));
}

std::int64_t carbonFlags(std::uint64_t flags)
{
    std::int64_t result = 0;
    if (flags & kCGEventFlagMaskControl) result |= controlKey;
    if (flags & kCGEventFlagMaskAlternate) result |= optionKey;
    if (flags & kCGEventFlagMaskShift) result |= shiftKey;
    if (flags & kCGEventFlagMaskCommand) result |= cmdKey;
    if (flags & kCGEventFlagMaskSecondaryFn) result |= kEventKeyModifierFnMask;
    return result;
}

} // namespace

OSXInputSourceBackend ParseOSXInputSourceBackend(const std::string& value)
{
    if (value.empty()) return OSXInputSourceBackend::DirectTIS;
    return parseMacInputSourceBackend(value);
}

const char* OSXInputSourceBackendName(OSXInputSourceBackend backend)
{
    return macInputSourceBackendName(backend);
}

OSXInputSourceShortcutResult ParseOSXInputSourceShortcut(CFPropertyListRef preferences)
{
    // 60 is the current preference-schema identifier for "previous input
    // source". It is not a public API identifier. No action-61/default fallback.
    CFTypeRef entry = field(preferences, CFSTR("60"));
    if (entry == nullptr) return {false, {}, "missing-shortcut-60"};
    if (field(entry, CFSTR("enabled")) != kCFBooleanTrue)
        return {false, {}, "disabled-or-malformed-shortcut"};
    CFTypeRef value = field(entry, CFSTR("value"));
    CFTypeRef type = field(value, CFSTR("type"));
    if (type == nullptr || CFGetTypeID(type) != CFStringGetTypeID()
        || !CFEqual(type, CFSTR("standard"))) return {false, {}, "unsupported-shortcut-type"};
    CFTypeRef params = field(value, CFSTR("parameters"));
    if (params == nullptr || CFGetTypeID(params) != CFArrayGetTypeID()
        || CFArrayGetCount(static_cast<CFArrayRef>(params)) != 3)
        return {false, {}, "malformed-shortcut-parameters"};
    auto array = static_cast<CFArrayRef>(params);
    std::int64_t character = 0, code = 0, modifiers = 0;
    if (!integer(CFArrayGetValueAtIndex(array, 0), character)
        || !integer(CFArrayGetValueAtIndex(array, 1), code)
        || !integer(CFArrayGetValueAtIndex(array, 2), modifiers)
        || character < 0 || character > 65535 || code < 0 || code > 127 || modifiers < 0)
        return {false, {}, "invalid-shortcut-numbers"};
    OSXInputSourceShortcut shortcut{static_cast<std::uint16_t>(code),
                                   static_cast<std::uint64_t>(modifiers)};
    if (!supported(shortcut)) return {false, {}, "unsupported-shortcut-chord"};
    return {true, shortcut, "preference-parsed-not-yet-registered"};
}

bool OSXInputSourceShortcutIsRegistered(const OSXInputSourceShortcut& shortcut,
                                        CFArrayRef symbolicHotKeys)
{
    if (!supported(shortcut) || symbolicHotKeys == nullptr
        || CFGetTypeID(symbolicHotKeys) != CFArrayGetTypeID()) return false;
    for (CFIndex i = 0; i < CFArrayGetCount(symbolicHotKeys); ++i) {
        CFTypeRef entry = CFArrayGetValueAtIndex(symbolicHotKeys, i);
        if (field(entry, kHISymbolicHotKeyEnabled) != kCFBooleanTrue) continue;
        std::int64_t code = 0, flags = 0;
        if (integer(field(entry, kHISymbolicHotKeyCode), code)
            && integer(field(entry, kHISymbolicHotKeyModifiers), flags)
            && code == shortcut.keyCode && flags == carbonFlags(shortcut.modifiers)) return true;
    }
    return false;
}

std::vector<OSXInputSourceShortcutEvent> MakeOSXInputSourceShortcutEvents(
    const OSXInputSourceShortcut& shortcut, std::uint64_t heldModifiers)
{
    if (heldModifiers != 0 || !supported(shortcut)) return {};
    struct Modifier { std::uint16_t key; std::uint64_t flag; };
    const Modifier modifiers[] = {
        {kVK_Control, kCGEventFlagMaskControl}, {kVK_Option, kCGEventFlagMaskAlternate},
        {kVK_Shift, kCGEventFlagMaskShift}, {kVK_Command, kCGEventFlagMaskCommand}};
    std::vector<OSXInputSourceShortcutEvent> events;
    std::uint64_t flags = 0;
    for (const auto& modifier : modifiers) {
        if (!(shortcut.modifiers & modifier.flag)) continue;
        flags |= modifier.flag;
        events.push_back({modifier.key, true, flags});
    }
    // Keep the stored function flag on the shortcut key pair. It is not a
    // request to tap the physical Fn/Globe key, which has its own system action.
    const auto keyFlags = flags | (shortcut.modifiers & kCGEventFlagMaskSecondaryFn);
    events.push_back({shortcut.keyCode, true, keyFlags});
    events.push_back({shortcut.keyCode, false, keyFlags});
    for (int i = 3; i >= 0; --i) {
        if (!(shortcut.modifiers & modifiers[i].flag)) continue;
        flags &= ~modifiers[i].flag;
        events.push_back({modifiers[i].key, false, flags});
    }
    return events;
}

} // namespace inputleap
