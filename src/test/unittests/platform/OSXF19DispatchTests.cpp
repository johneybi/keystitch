/*
 * InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "platform/OSXInputSourceSelection.h"
#include "platform/OSXKeyState.h"
#include "server/KeyRemapper.h"
#include "test/mock/inputleap/MockEventQueue.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

namespace inputleap {
namespace {

// Real KeyRemapper/KeyState/KeyMap/OSXKeyState::fakeKey, inert OS boundaries.
// Source IDs below are fixture state, not observations of the user's IME.
class InertF19KeyState : public OSXKeyState {
public:
    struct Delivery {
        std::uint8_t key;
        bool down;
        CGEventFlags flags;
        std::string source;
    };

    explicit InertF19KeyState(IEventQueue* events) : OSXKeyState(events) {
        updateKeyMap();
        updateKeyState();
    }

    std::string source = kOSXAppleRomanABC;
    bool selectionSucceeds = true;
    std::vector<Delivery> deliveries;
    std::vector<std::int32_t> offsets;
    mutable std::vector<std::string> trace;
    std::vector<CGEventFlags> flagsAtSelection;
    int settles = 0;

    void dispatch(std::uint8_t key, bool down, bool repeat = false) {
        // Production maps macOS virtual keys to buttons with offset one.
        OSXKeyState::fakeKey(Keystroke(key + 1, down, repeat, 0));
    }

    void relay(const KeyRemapper::KeyEventList& events) {
        for (const auto& event : events) {
            switch (event.m_type) {
            case KeyRemapper::KeyEvent::kDown:
                fakeKeyDown(event.m_id, event.m_mask, event.m_button);
                break;
            case KeyRemapper::KeyEvent::kUp:
                fakeKeyUp(event.m_button);
                break;
            case KeyRemapper::KeyEvent::kRepeat:
                fakeKeyRepeat(event.m_id, event.m_mask, event.m_count, event.m_button);
                break;
            }
        }
    }

    std::int32_t pollActiveGroup() const override { return 0; }
    KeyModifierMask pollActiveModifiers() const override { return 0; }
    void pollPressedKeys(KeyButtonSet&) const override {}
    bool fakeCtrlAltDel() override { return false; }
    bool fakeMediaKey(KeyID) override { return false; }

protected:
    void getKeyMap(KeyMap& map) override {
        auto add = [&](KeyID id, std::uint8_t key, KeyModifierMask sensitive = 0) {
            KeyMap::KeyItem item{};
            item.m_id = id;
            item.m_button = key + 1;
            item.m_group = 0;
            item.m_sensitive = sensitive;
            KeyMap::initModifierKey(item);
            map.addKeyEntry(item);
        };
        add(kKeyF19, kVK_F19);
        add('a', kVK_ANSI_A, KeyModifierShift);
        add(kKeyShift_L, kVK_Shift);
        add(kKeyShift_R, kVK_RightShift);
        add(kKeyControl_L, kVK_Control);
        add(kKeyControl_R, kVK_RightControl);
        add(kKeyAlt_L, kVK_Option);
        add(kKeyAlt_R, kVK_RightOption);
        add(kKeySuper_L, kVK_Command);
        add(kKeySuper_R, kVK_RightCommand);
    }

    void postKeyboardEvent(std::uint8_t key, bool down, CGEventFlags flags) override {
        trace.push_back(down ? "key-down" : "key-up");
        deliveries.push_back({key, down, flags, source});
    }

    std::string describeInputSourceState() const override {
        trace.push_back("snapshot");
        return source;
    }

    bool cycleInputSource(std::int32_t offset) override {
        trace.push_back("select");
        offsets.push_back(offset);
        flagsAtSelection.push_back(getModifierStateAsOSXFlags());
        if (!selectionSucceeds) {
            return false;
        }
        // Reuse the production pure policy, but never the TIS/CF adapter.
        const auto selected = PickOSXInputSourceTarget(
            {{kOSXAppleRomanABC, true, true}, {kOSXAppleKorean, true, true}},
            source, "", "", offset);
        if (selected.m_targetId.empty()) {
            return false;
        }
        source = selected.m_targetId;
        return true;
    }

    void waitForInputSourceSettling() override {
        trace.push_back("settle");
        ++settles;
    }
};

class OSXF19DispatchTests : public ::testing::Test {
protected:
    ::testing::NiceMock<MockEventQueue> events;
    InertF19KeyState keys{&events};
    void tap() {
        keys.dispatch(kVK_F19, true);
        keys.dispatch(kVK_F19, false);
    }
    void letter() {
        keys.dispatch(kVK_ANSI_A, true);
        keys.dispatch(kVK_ANSI_A, false);
    }
};

TEST_F(OSXF19DispatchTests, realDispatcherSelectsBothDirectionsBeforeTheFirstKey)
{
    for (const std::string initial : {kOSXAppleRomanABC, kOSXAppleKorean}) {
        keys.source = initial;
        keys.trace.clear();
        keys.deliveries.clear();
        tap();
        letter();
        EXPECT_EQ((std::vector<std::string>{"snapshot", "select", "snapshot", "settle",
                                           "key-down", "key-up"}), keys.trace);
        ASSERT_EQ(2u, keys.deliveries.size());
        const std::string expected = initial == kOSXAppleRomanABC
            ? kOSXAppleKorean : kOSXAppleRomanABC;
        for (const auto& event : keys.deliveries) {
            EXPECT_EQ(kVK_ANSI_A, event.key);
            EXPECT_EQ(expected, event.source);
            EXPECT_EQ(0u, event.flags);
        }
        EXPECT_TRUE(keys.deliveries[0].down);
        EXPECT_FALSE(keys.deliveries[1].down);
    }
    EXPECT_EQ((std::vector<std::int32_t>{1, 1}), keys.offsets);
    EXPECT_EQ(2, keys.settles);
}

TEST_F(OSXF19DispatchTests, repeatsAndReleasesDoNotSelectSettleOrPostF19)
{
    keys.dispatch(kVK_F19, false);
    keys.dispatch(kVK_F19, true, true);
    keys.dispatch(kVK_F19, false, true);
    EXPECT_TRUE(keys.trace.empty());
    EXPECT_TRUE(keys.offsets.empty());
    EXPECT_TRUE(keys.deliveries.empty());
    EXPECT_EQ(0, keys.settles);

    keys.dispatch(kVK_F19, true);
    keys.dispatch(kVK_F19, true, true);
    keys.dispatch(kVK_F19, false);
    letter();
    EXPECT_EQ(1u, keys.offsets.size());
    EXPECT_EQ(1, keys.settles);
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
}

TEST_F(OSXF19DispatchTests, failedSelectionDoesNotRetrySettleOrSwallowTheNextKey)
{
    keys.selectionSucceeds = false;
    tap();
    letter();
    EXPECT_EQ((std::vector<std::string>{"snapshot", "select", "snapshot", "key-down", "key-up"}),
              keys.trace);
    EXPECT_EQ((std::vector<std::int32_t>{1}), keys.offsets);
    EXPECT_EQ(0, keys.settles);
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(kOSXAppleRomanABC, keys.deliveries[0].source);
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
    EXPECT_TRUE(keys.deliveries[0].down);
    EXPECT_FALSE(keys.deliveries[1].down);
}

TEST_F(OSXF19DispatchTests, ordinaryKeyDoesNotReadOrSwitchInputSources)
{
    letter();
    EXPECT_EQ((std::vector<std::string>{"key-down", "key-up"}), keys.trace);
    EXPECT_TRUE(keys.offsets.empty());
    EXPECT_EQ(0, keys.settles);
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
}

TEST_F(OSXF19DispatchTests, heldModifiersSurviveSuccessAndFailureWithoutSyntheticF19)
{
    struct Modifier { std::uint8_t key; CGEventFlags flag; };
    for (bool succeeds : {true, false}) {
        keys.selectionSucceeds = succeeds;
        for (const auto& modifier : {
                Modifier{kVK_Shift, kCGEventFlagMaskShift},
                Modifier{kVK_Control, kCGEventFlagMaskControl},
                Modifier{kVK_RightOption, kCGEventFlagMaskAlternate},
                Modifier{kVK_RightCommand, kCGEventFlagMaskCommand}}) {
            keys.deliveries.clear();
            keys.dispatch(modifier.key, true);
            tap();
            letter();
            keys.dispatch(modifier.key, false);
            ASSERT_EQ(4u, keys.deliveries.size());
            EXPECT_EQ(modifier.flag, keys.flagsAtSelection.back());
            EXPECT_EQ(modifier.flag, keys.deliveries[1].flags);
            EXPECT_EQ(modifier.flag, keys.deliveries[2].flags);
            EXPECT_EQ(0u, keys.deliveries.back().flags);
            EXPECT_EQ(0u, keys.getModifierStateAsOSXFlags());
            for (const auto& event : keys.deliveries) {
                EXPECT_NE(kVK_F19, event.key);
            }
        }
    }
}

TEST_F(OSXF19DispatchTests, duplicateF19DownThroughKeyStateIsConsumedAsRepeat)
{
    keys.fakeKeyDown(kKeyF19, 0, 100);
    keys.fakeKeyDown(kKeyF19, 0, 100);
    EXPECT_TRUE(keys.fakeKeyRepeat(kKeyF19, 0, 3, 100));
    EXPECT_TRUE(keys.fakeKeyUp(100));
    keys.fakeKeyDown('a', 0, 101);
    EXPECT_TRUE(keys.fakeKeyUp(101));
    EXPECT_EQ(1u, keys.offsets.size());
    EXPECT_EQ(1, keys.settles);
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
    EXPECT_EQ(0, keys.getKeyState(kVK_F19 + 1));
    EXPECT_EQ(0, keys.getKeyState(kVK_ANSI_A + 1));
}

TEST_F(OSXF19DispatchTests, realRemapperThroughOSXDispatcherKeepsRepeatedTapAndFirstKeyPairs)
{
    KeyRemapper remapper;
    KeyRemapConfig config;
    config.addTapRule("mac", kKeyAlt_R, kKeyF19, kKeySuper_R);
    config.addTapRule("mac", kKeyHangul, kKeyF19, kKeySuper_R);
    remapper.setConfig(config);
    int cycles = 0;
    for (KeyID sourceKey : {kKeyAlt_R, kKeyHangul}) {
        const KeyModifierMask mask = sourceKey == kKeyAlt_R ? KeyModifierAlt : 0;
        for (int i = 0; i < 32; ++i) {
            keys.deliveries.clear();
            keys.trace.clear();
            keys.relay(remapper.remapKeyDown("mac", sourceKey, mask, 100));
            keys.relay(remapper.remapKeyUp("mac", sourceKey, 0, 100));
            keys.relay(remapper.remapKeyDown("mac", 'a', 0, 101));
            keys.relay(remapper.remapKeyUp("mac", 'a', 0, 101));
            ++cycles;
            EXPECT_EQ((std::vector<std::string>{"snapshot", "select", "snapshot", "settle",
                                               "key-down", "key-up"}), keys.trace);
            ASSERT_EQ(2u, keys.deliveries.size());
            EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
            EXPECT_TRUE(keys.deliveries[0].down);
            EXPECT_FALSE(keys.deliveries[1].down);
            EXPECT_EQ(cycles % 2 ? kOSXAppleKorean : kOSXAppleRomanABC, keys.deliveries[0].source);
            EXPECT_EQ(0u, keys.deliveries[0].flags);
            EXPECT_EQ(0u, keys.getActiveModifiers());
            EXPECT_EQ(0u, keys.getModifierStateAsOSXFlags());
            EXPECT_EQ(0, keys.getKeyState(kVK_F19 + 1));
            EXPECT_EQ(0, keys.getKeyState(kVK_ANSI_A + 1));
        }
    }
    EXPECT_EQ(64u, keys.offsets.size());
    EXPECT_EQ(64, keys.settles);
    for (auto offset : keys.offsets) {
        EXPECT_EQ(1, offset);
    }
}

TEST_F(OSXF19DispatchTests, remappedTapRestoresShiftAndTheFollowingUnmodifiedLetter)
{
    KeyRemapper remapper;
    KeyRemapConfig config;
    config.addTapRule("mac", kKeyAlt_R, kKeyF19, kKeySuper_R);
    remapper.setConfig(config);
    keys.relay(remapper.remapKeyDown("mac", kKeyShift_L, KeyModifierShift, 102));
    keys.relay(remapper.remapKeyDown("mac", kKeyAlt_R, KeyModifierShift | KeyModifierAlt, 100));
    keys.relay(remapper.remapKeyUp("mac", kKeyAlt_R, KeyModifierShift, 100));
    EXPECT_EQ(KeyModifierShift, keys.getActiveModifiers());
    EXPECT_EQ(kCGEventFlagMaskShift, keys.getModifierStateAsOSXFlags());
    keys.relay(remapper.remapKeyUp("mac", kKeyShift_L, 0, 102));
    keys.deliveries.clear();
    keys.relay(remapper.remapKeyDown("mac", 'a', 0, 101));
    keys.relay(remapper.remapKeyUp("mac", 'a', 0, 101));
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(1u, keys.offsets.size());
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
    EXPECT_EQ(0u, keys.deliveries[0].flags);
    EXPECT_EQ(0u, keys.deliveries[1].flags);
    EXPECT_EQ(0u, keys.getActiveModifiers());
    EXPECT_EQ(0u, keys.getModifierStateAsOSXFlags());
}

TEST_F(OSXF19DispatchTests, remappedHoldAndCleanupDoNotBecomeAToggleOrLeakAModifier)
{
    KeyRemapper remapper;
    KeyRemapConfig config;
    config.addTapRule("mac", kKeyAlt_R, kKeyF19, kKeySuper_R);
    remapper.setConfig(config);
    keys.relay(remapper.remapKeyDown("mac", kKeyAlt_R, KeyModifierAlt, 100));
    for (const auto& screen : remapper.flushPendingTapHolds()) {
        keys.relay(screen.second);
    }
    EXPECT_EQ(KeyModifierSuper, keys.getActiveModifiers());
    EXPECT_EQ(kCGEventFlagMaskCommand, keys.getModifierStateAsOSXFlags());
    EXPECT_TRUE(keys.offsets.empty());
    keys.fakeAllKeysUp();
    remapper.resetScreen("mac");
    EXPECT_EQ(0u, keys.getActiveModifiers());
    EXPECT_EQ(0u, keys.getModifierStateAsOSXFlags());
    EXPECT_EQ(0, keys.getKeyState(kVK_RightCommand + 1));
    keys.deliveries.clear();
    keys.relay(remapper.remapKeyDown("mac", kKeyAlt_R, KeyModifierAlt, 100));
    keys.relay(remapper.remapKeyUp("mac", kKeyAlt_R, 0, 100));
    keys.relay(remapper.remapKeyDown("mac", 'a', 0, 101));
    keys.relay(remapper.remapKeyUp("mac", 'a', 0, 101));
    EXPECT_EQ(1u, keys.offsets.size());
    ASSERT_EQ(2u, keys.deliveries.size());
    EXPECT_EQ(kVK_ANSI_A, keys.deliveries[0].key);
    EXPECT_EQ(0u, keys.deliveries[0].flags);
    EXPECT_EQ(0u, keys.getModifierStateAsOSXFlags());
}

} // namespace
} // namespace inputleap
