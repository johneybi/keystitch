/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "base/MacInputSourceBackend.h"
#include <gtest/gtest.h>

namespace inputleap {

TEST(MacInputSourceBackendTests, ExplicitChoiceWinsOverInheritedEnvironment)
{
    for (auto mode : {MacInputSourceBackend::NativeShortcut, MacInputSourceBackend::DirectTIS}) {
        for (auto env : {"direct-tis", "native-shortcut", "invalid", ""}) {
            const auto selection = resolveMacInputSourceBackend(mode, env);
            EXPECT_EQ(mode, selection.backend);
            EXPECT_STREQ("command-line", selection.source);
        }
    }
}

TEST(MacInputSourceBackendTests, StandaloneCompatibilityAndInvalidEnvironment)
{
    EXPECT_EQ(MacInputSourceBackend::DirectTIS, resolveMacInputSourceBackend(std::nullopt, nullptr).backend);
    EXPECT_EQ(MacInputSourceBackend::DirectTIS, resolveMacInputSourceBackend(std::nullopt, "").backend);
    const auto selected = resolveMacInputSourceBackend(std::nullopt, "native-shortcut");
    EXPECT_EQ(MacInputSourceBackend::NativeShortcut, selected.backend);
    EXPECT_STREQ("environment", selected.source);
    EXPECT_EQ(MacInputSourceBackend::Invalid, resolveMacInputSourceBackend(std::nullopt, "broken").backend);
}

TEST(MacInputSourceBackendTests, PersistentAndCliValuesMustBeExplicit)
{
    for (auto value : {"", "auto", "native", " native-shortcut", "DIRECT-TIS"}) {
        EXPECT_EQ(MacInputSourceBackend::Invalid, parseMacInputSourceBackend(value));
    }
    for (auto mode : {MacInputSourceBackend::DirectTIS, MacInputSourceBackend::NativeShortcut}) {
        EXPECT_EQ(mode, parseMacInputSourceBackend(macInputSourceBackendName(mode)));
    }
}

} // namespace inputleap
