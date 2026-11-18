/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform/OSXKeyState.h"
#include "test/mock/inputleap/MockEventQueue.h"
#include <gtest/gtest.h>

namespace inputleap {
namespace {
class ConfiguredKeyState : public OSXKeyState {
public:
    using OSXKeyState::OSXKeyState;
    using OSXKeyState::inputSourceBackend;
};
}

TEST(OSXInputSourceConfigurationTests, ExplicitChoiceReachesActualKeyState)
{
    // Constructor/configuration only: no screen, input source query or event post.
    MockEventQueue events;
    ConfiguredKeyState native(&events, MacInputSourceBackend::NativeShortcut);
    ConfiguredKeyState legacy(&events, MacInputSourceBackend::DirectTIS);
    EXPECT_EQ(MacInputSourceBackend::NativeShortcut, native.inputSourceBackend());
    EXPECT_EQ(MacInputSourceBackend::DirectTIS, legacy.inputSourceBackend());
}

} // namespace inputleap
