/* InputLeap -- mouse and keyboard sharing utility
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "platform/OSXInputSourceTransition.h"
#include <gtest/gtest.h>

namespace inputleap {
namespace {
using Result = OSXInputSourceTransition::Result;

TEST(OSXInputSourceTransitionTests, unchangedSourceDoesNotBecomeReadyAfterTheOldSleep)
{
    OSXInputSourceTransition gate;
    gate.begin("ABC", 0);
    EXPECT_EQ(Result::Waiting, gate.observe("ABC", 0.075));
    EXPECT_EQ(Result::Waiting, gate.observe("ABC", 0.299));
    EXPECT_EQ(Result::TimedOut, gate.observe("ABC", 0.301));
}

TEST(OSXInputSourceTransitionTests, observedChangeCanFinishBeforeTheOldFixedSleep)
{
    OSXInputSourceTransition gate;
    gate.begin("ABC", 0);
    EXPECT_EQ(Result::Waiting, gate.observe("Korean", 0.004));
    EXPECT_EQ(Result::Waiting, gate.observe("Korean", 0.020));
    EXPECT_EQ(Result::Changed, gate.observe("Korean", 0.025));
}

TEST(OSXInputSourceTransitionTests, missingAndRevertedSourcesRestartTheStabilityWindow)
{
    for (const auto* interrupt : {"", "ABC", "Other"}) {
        OSXInputSourceTransition gate;
        gate.begin("ABC", 10);
        EXPECT_EQ(Result::Waiting, gate.observe("Korean", 10.004));
        EXPECT_EQ(Result::Waiting, gate.observe(interrupt, 10.019));
        EXPECT_EQ(Result::Waiting, gate.observe("Korean", 10.025));
        EXPECT_EQ(Result::Waiting, gate.observe("Korean", 10.040));
        EXPECT_EQ(Result::Changed, gate.observe("Korean", 10.046));
    }
}

TEST(OSXInputSourceTransitionTests, flappingAndLateChangesCannotExtendTheHardDeadline)
{
    OSXInputSourceTransition gate;
    gate.begin("ABC", 0);
    EXPECT_EQ(Result::Waiting, gate.observe("Korean", 0.290));
    EXPECT_EQ(Result::TimedOut, gate.observe("Korean", 0.311));
    gate.begin("ABC", 1);
    for (int i = 1; i < 30; ++i) {
        EXPECT_EQ(Result::Waiting, gate.observe(i % 2 ? "Korean" : "ABC", 1 + i * 0.01));
    }
    EXPECT_EQ(Result::TimedOut, gate.observe("Korean", 1.301));
}

TEST(OSXInputSourceTransitionTests, aNewToggleRequiresANewChangeInEitherDirection)
{
    OSXInputSourceTransition gate;
    gate.begin("ABC", 0);
    gate.observe("Korean", 0.004);
    EXPECT_EQ(Result::Changed, gate.observe("Korean", 0.025));
    gate.begin("Korean", 1);
    EXPECT_EQ(Result::Waiting, gate.observe("Korean", 1.100));
    EXPECT_EQ(Result::Waiting, gate.observe("ABC", 1.104));
    EXPECT_EQ(Result::Changed, gate.observe("ABC", 1.125));
}
} // namespace
} // namespace inputleap
