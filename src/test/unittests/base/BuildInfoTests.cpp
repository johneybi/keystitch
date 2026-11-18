/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common/BuildInfo.h"
#include <gtest/gtest.h>

namespace inputleap {

TEST(BuildInfoTests, OrdinaryInvocationIsNotHandled)
{
    const char* argv[] = {"app", "--other"};
    EXPECT_FALSE(handleBuildInfoRequest(2, argv, BuildComponent::Client).has_value());
}

TEST(BuildInfoTests, StandaloneProbeReturnsJsonAndSuccess)
{
    const char* argv[] = {"app", "--build-info"};
    testing::internal::CaptureStdout();
    const auto result = handleBuildInfoRequest(2, argv, BuildComponent::Gui);
    const auto output = testing::internal::GetCapturedStdout();
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(0, *result);
    EXPECT_NE(std::string::npos, output.find("KeyStitchBuildInfo:v1"));
    EXPECT_NE(std::string::npos, output.find("\"component\":\"gui\""));
}

TEST(BuildInfoTests, ExtraArgumentsCannotFallThroughToEngineStartup)
{
    const char* argv[] = {"app", "--build-info", "server.example"};
    testing::internal::CaptureStderr();
    const auto result = handleBuildInfoRequest(3, argv, BuildComponent::Client);
    const auto error = testing::internal::GetCapturedStderr();
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(0, *result);
    EXPECT_NE(std::string::npos, error.find("must be used alone"));
}

} // namespace inputleap
