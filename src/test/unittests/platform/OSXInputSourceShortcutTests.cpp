/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "platform/OSXInputSourceShortcut.h"

#include <Carbon/Carbon.h>
#include <gtest/gtest.h>
#include <set>

namespace inputleap {
namespace {

// In-memory property lists only. Never CFPreferences/CopySymbolicHotKeys/TIS.
class FixturePlist {
public:
    explicit FixturePlist(const std::string& body) {
        const auto xml = "<plist version=\"1.0\">" + body + "</plist>";
        CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(xml.data()), xml.size());
        value = CFPropertyListCreateWithData(nullptr, data, kCFPropertyListImmutable, nullptr, nullptr);
        CFRelease(data);
    }
    ~FixturePlist() { if (value != nullptr) CFRelease(value); }
    FixturePlist(const FixturePlist&) = delete;
    FixturePlist& operator=(const FixturePlist&) = delete;
    CFPropertyListRef value = nullptr;
};

std::string preferences(const std::string& code = "<integer>49</integer>",
                        const std::string& flags = "<integer>262144</integer>",
                        const std::string& enabled = "<true/>",
                        const std::string& type = "<string>standard</string>",
                        const std::string& action = "60")
{
    return "<dict><key>" + action + "</key><dict><key>enabled</key>" + enabled
        + "<key>value</key><dict><key>type</key>" + type
        + "<key>parameters</key><array><integer>32</integer>" + code + flags
        + "</array></dict></dict></dict>";
}

std::string registration(const std::string& code = "<integer>49</integer>",
                         const std::string& flags = "<integer>4096</integer>",
                         const std::string& enabled = "<true/>")
{
    return "<array><dict><key>kHISymbolicHotKeyEnabled</key>" + enabled
        + "<key>kHISymbolicHotKeyCode</key>" + code
        + "<key>kHISymbolicHotKeyModifiers</key>" + flags + "</dict></array>";
}

TEST(OSXInputSourceShortcutTests, backendSelectionIsExplicitAndInvalidValuesFailClosed)
{
    EXPECT_EQ(OSXInputSourceBackend::DirectTIS, ParseOSXInputSourceBackend(""));
    EXPECT_EQ(OSXInputSourceBackend::DirectTIS, ParseOSXInputSourceBackend("direct-tis"));
    EXPECT_EQ(OSXInputSourceBackend::NativeShortcut, ParseOSXInputSourceBackend("native-shortcut"));
    for (const auto* value : {"auto", "native", " native-shortcut", "DIRECT-TIS"}) {
        EXPECT_EQ(OSXInputSourceBackend::Invalid, ParseOSXInputSourceBackend(value));
    }
    EXPECT_STREQ("native-shortcut", OSXInputSourceBackendName(OSXInputSourceBackend::NativeShortcut));
    EXPECT_STREQ("invalid", OSXInputSourceBackendName(OSXInputSourceBackend::Invalid));
}

TEST(OSXInputSourceShortcutTests, configuredControlSpaceIsParsedButNotAssumedRegistered)
{
    FixturePlist pref(preferences());
    ASSERT_NE(nullptr, pref.value);
    const auto result = ParseOSXInputSourceShortcut(pref.value);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(kVK_Space, result.shortcut.keyCode);
    EXPECT_EQ(kCGEventFlagMaskControl, result.shortcut.modifiers);
    EXPECT_EQ("preference-parsed-not-yet-registered", result.reason);
    EXPECT_FALSE(OSXInputSourceShortcutIsRegistered(result.shortcut, nullptr));
}

TEST(OSXInputSourceShortcutTests, missingDisabledOrWrongSchemaDoesNotInventADefault)
{
    EXPECT_FALSE(ParseOSXInputSourceShortcut(nullptr).valid);
    for (const auto& body : {std::string("<array/>"), std::string("<dict/>"),
            preferences("<integer>49</integer>", "<integer>262144</integer>", "<false/>"),
            preferences("<integer>49</integer>", "<integer>262144</integer>", "<integer>1</integer>"),
            preferences("<integer>49</integer>", "<integer>262144</integer>", "<true/>", "<string>other</string>"),
            preferences("<integer>49</integer>", "<integer>262144</integer>", "<true/>", "<string>standard</string>", "61"),
            std::string("<dict><key>60</key><string>invalid</string></dict>"),
            std::string("<dict><key>60</key><dict><key>enabled</key><true/><key>value</key><dict><key>type</key><string>standard</string><key>parameters</key><array/></dict></dict></dict>")}) {
        FixturePlist pref(body);
        ASSERT_NE(nullptr, pref.value);
        EXPECT_FALSE(ParseOSXInputSourceShortcut(pref.value).valid) << body;
    }
}

TEST(OSXInputSourceShortcutTests, shortcutNumbersMustBeExactIntegersInRange)
{
    for (const auto* code : {"<integer>-1</integer>", "<integer>128</integer>",
            "<real>49</real>", "<true/>", "<string>49</string>"}) {
        FixturePlist pref(preferences(code));
        ASSERT_NE(nullptr, pref.value);
        EXPECT_FALSE(ParseOSXInputSourceShortcut(pref.value).valid);
    }
    for (const auto* flags : {"<integer>-1</integer>", "<real>262144</real>",
            "<true/>", "<string>262144</string>", "<integer>8388608</integer>"}) {
        FixturePlist pref(preferences("<integer>49</integer>", flags));
        ASSERT_NE(nullptr, pref.value);
        EXPECT_FALSE(ParseOSXInputSourceShortcut(pref.value).valid);
    }
}

TEST(OSXInputSourceShortcutTests, rejectsTextModifierAndLockKeysButSupportsConfiguredF19)
{
    const int invalidKeys[] = {kVK_ANSI_A, kVK_Return, kVK_Control, kVK_Shift, kVK_CapsLock, kVK_Function};
    for (int key : invalidKeys) {
        FixturePlist pref(preferences("<integer>" + std::to_string(key) + "</integer>"));
        ASSERT_NE(nullptr, pref.value);
        EXPECT_FALSE(ParseOSXInputSourceShortcut(pref.value).valid) << key;
    }
    for (const auto* flags : {"<integer>0</integer>", "<integer>131072</integer>"}) {
        FixturePlist pref(preferences("<integer>49</integer>", flags));
        EXPECT_FALSE(ParseOSXInputSourceShortcut(pref.value).valid);
    }
    FixturePlist f19(preferences("<integer>80</integer>", "<integer>0</integer>"));
    const auto result = ParseOSXInputSourceShortcut(f19.value);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(kVK_F19, result.shortcut.keyCode);
    EXPECT_EQ(0u, result.shortcut.modifiers);
}

TEST(OSXInputSourceShortcutTests, registeredHotkeysUseCarbonModifiersNotQuartzFlags)
{
    const OSXInputSourceShortcut shortcut{kVK_Space, kCGEventFlagMaskControl};
    FixturePlist valid(registration());
    ASSERT_NE(nullptr, valid.value);
    EXPECT_TRUE(OSXInputSourceShortcutIsRegistered(shortcut, static_cast<CFArrayRef>(valid.value)));
    for (const auto& body : {registration("<integer>49</integer>", "<integer>262144</integer>"),
            registration("<integer>49</integer>", "<integer>4096</integer>", "<false/>"),
            registration("<integer>48</integer>"), registration("<real>49</real>"),
            std::string("<array><string>invalid</string></array>"), std::string("<dict/>")}) {
        FixturePlist list(body);
        ASSERT_NE(nullptr, list.value);
        EXPECT_FALSE(OSXInputSourceShortcutIsRegistered(shortcut, static_cast<CFArrayRef>(list.value)));
    }
}

TEST(OSXInputSourceShortcutTests, configuredF19KeepsStoredFunctionFlagAndChecksCarbonRegistration)
{
    // Observed read-only on the host: System Settings displays F19, while
    // action 60 stores [65535, 80, 8388608] and the public list uses 0x20000.
    FixturePlist pref("<dict><key>60</key><dict><key>enabled</key><true/>"
        "<key>value</key><dict><key>type</key><string>standard</string>"
        "<key>parameters</key><array><integer>65535</integer><integer>80</integer>"
        "<integer>8388608</integer></array></dict></dict></dict>");
    const auto result = ParseOSXInputSourceShortcut(pref.value);
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(kVK_F19, result.shortcut.keyCode);
    EXPECT_EQ(kCGEventFlagMaskSecondaryFn, result.shortcut.modifiers);
    FixturePlist registered(registration("<integer>80</integer>", "<integer>131072</integer>"));
    EXPECT_TRUE(OSXInputSourceShortcutIsRegistered(result.shortcut, static_cast<CFArrayRef>(registered.value)));
    FixturePlist missingFlag(registration("<integer>80</integer>", "<integer>0</integer>"));
    EXPECT_FALSE(OSXInputSourceShortcutIsRegistered(result.shortcut, static_cast<CFArrayRef>(missingFlag.value)));
    FixturePlist quartzFlags(registration("<integer>80</integer>", "<integer>8388608</integer>"));
    EXPECT_FALSE(OSXInputSourceShortcutIsRegistered(result.shortcut, static_cast<CFArrayRef>(quartzFlags.value)));
}

TEST(OSXInputSourceShortcutTests, functionFlagStaysOnFunctionKeyWithoutSynthesizingGlobeTaps)
{
    const std::uint64_t masks[] = {kCGEventFlagMaskControl, kCGEventFlagMaskAlternate,
                                  kCGEventFlagMaskShift, kCGEventFlagMaskCommand};
    for (unsigned bits = 0; bits < 16; ++bits) {
        std::uint64_t flags = kCGEventFlagMaskSecondaryFn;
        for (unsigned i = 0; i < 4; ++i) if (bits & (1u << i)) flags |= masks[i];
        const auto plan = MakeOSXInputSourceShortcutEvents({kVK_F19, flags}, 0);
        ASSERT_FALSE(plan.empty());
        std::set<std::uint16_t> held;
        unsigned functionEvents = 0;
        for (const auto& event : plan) {
            EXPECT_NE(kVK_Function, event.keyCode);
            if (event.keyCode == kVK_F19) {
                ++functionEvents;
                EXPECT_EQ(flags, event.flags);
            } else {
                EXPECT_EQ(0u, event.flags & kCGEventFlagMaskSecondaryFn);
            }
            if (event.down) EXPECT_TRUE(held.insert(event.keyCode).second);
            else EXPECT_EQ(1u, held.erase(event.keyCode));
        }
        EXPECT_EQ(2u, functionEvents);
        EXPECT_TRUE(held.empty());
        EXPECT_LE(plan.size(), 10u);
    }
    // No general Fn/Globe shortcut support: only the F1-F20 event flag.
    EXPECT_TRUE(MakeOSXInputSourceShortcutEvents({kVK_Space,
        kCGEventFlagMaskControl | kCGEventFlagMaskSecondaryFn}, 0).empty());
    EXPECT_TRUE(MakeOSXInputSourceShortcutEvents({kVK_F19,
        kCGEventFlagMaskSecondaryFn}, kCGEventFlagMaskSecondaryFn).empty());
}

TEST(OSXInputSourceShortcutTests, controlOptionSpaceHasBalancedKeysAndFlags)
{
    const auto flags = kCGEventFlagMaskControl | kCGEventFlagMaskAlternate;
    const auto plan = MakeOSXInputSourceShortcutEvents({kVK_Space, flags}, 0);
    ASSERT_EQ(6u, plan.size());
    EXPECT_EQ(kVK_Control, plan[0].keyCode);
    EXPECT_EQ(kCGEventFlagMaskControl, plan[0].flags);
    EXPECT_EQ(kVK_Option, plan[1].keyCode);
    EXPECT_EQ(flags, plan[1].flags);
    EXPECT_EQ(kVK_Space, plan[2].keyCode);
    EXPECT_TRUE(plan[2].down);
    EXPECT_EQ(flags, plan[3].flags);
    EXPECT_FALSE(plan[3].down);
    EXPECT_EQ(kVK_Option, plan[4].keyCode);
    EXPECT_EQ(kCGEventFlagMaskControl, plan[4].flags);
    EXPECT_EQ(kVK_Control, plan[5].keyCode);
    EXPECT_EQ(0u, plan[5].flags);
}

TEST(OSXInputSourceShortcutTests, allSupportedModifierCombinationsReleaseEveryKeyExactlyOnce)
{
    const std::uint64_t masks[] = {kCGEventFlagMaskControl, kCGEventFlagMaskAlternate,
                                  kCGEventFlagMaskShift, kCGEventFlagMaskCommand};
    for (unsigned bits = 0; bits < 16; ++bits) {
        std::uint64_t flags = 0;
        for (unsigned i = 0; i < 4; ++i) if (bits & (1u << i)) flags |= masks[i];
        const auto plan = MakeOSXInputSourceShortcutEvents({kVK_F19, flags}, 0);
        ASSERT_FALSE(plan.empty());
        std::set<std::uint16_t> held;
        for (const auto& event : plan) {
            if (event.down) EXPECT_TRUE(held.insert(event.keyCode).second);
            else EXPECT_EQ(1u, held.erase(event.keyCode));
        }
        EXPECT_TRUE(held.empty());
        EXPECT_EQ(0u, plan.back().flags);
        EXPECT_LE(plan.size(), 10u);
    }
}

TEST(OSXInputSourceShortcutTests, plannerRejectsHeldModifiersAndUnsupportedChords)
{
    for (const auto flag : {kCGEventFlagMaskShift, kCGEventFlagMaskControl,
            kCGEventFlagMaskAlternate, kCGEventFlagMaskCommand, kCGEventFlagMaskAlphaShift}) {
        EXPECT_TRUE(MakeOSXInputSourceShortcutEvents({kVK_Space, kCGEventFlagMaskControl}, flag).empty());
    }
    EXPECT_TRUE(MakeOSXInputSourceShortcutEvents({kVK_ANSI_A, kCGEventFlagMaskCommand}, 0).empty());
    EXPECT_TRUE(MakeOSXInputSourceShortcutEvents({kVK_Space, 0}, 0).empty());
}

} // namespace
} // namespace inputleap
