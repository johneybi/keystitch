/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "inputleap/ArgParser.h"
#include "inputleap/ClientArgs.h"
#include "inputleap/ServerArgs.h"
#include <gtest/gtest.h>

namespace inputleap {

TEST(InputSourceArgumentTests, AbsentOptionPreservesStandaloneChoice)
{
    ArgParser parser(nullptr);
    ClientArgs args;
    const char* argv[] = {"client", "localhost"};
    ASSERT_TRUE(parser.parseClientArgs(args, 2, argv));
    EXPECT_FALSE(args.mac_input_source_backend.has_value());
}

TEST(InputSourceArgumentTests, PlatformAcceptsOrRejectsExplicitMacOption)
{
    ArgParser parser(nullptr);
    ClientArgs client;
    ServerArgs server;
    const char* clientArgv[] = {"client", "--macos-ime-backend", "native-shortcut", "localhost"};
    const char* serverArgv[] = {"server", "--macos-ime-backend", "direct-tis"};
#if WINAPI_CARBON
    ASSERT_TRUE(parser.parseClientArgs(client, 4, clientArgv));
    ASSERT_TRUE(parser.parseServerArgs(server, 3, serverArgv));
    ASSERT_TRUE(client.mac_input_source_backend.has_value());
    ASSERT_TRUE(server.mac_input_source_backend.has_value());
    EXPECT_EQ(MacInputSourceBackend::NativeShortcut, *client.mac_input_source_backend);
    EXPECT_EQ(MacInputSourceBackend::DirectTIS, *server.mac_input_source_backend);
#else
    EXPECT_FALSE(parser.parseClientArgs(client, 4, clientArgv));
    EXPECT_FALSE(parser.parseServerArgs(server, 3, serverArgv));
#endif
}

TEST(InputSourceArgumentTests, InvalidMissingAndDuplicateValuesFail)
{
    ArgParser parser(nullptr);
    for (const char* value : {"", "auto", "native", "DIRECT-TIS"}) {
        ClientArgs args;
        const char* argv[] = {"client", "--macos-ime-backend", value, "localhost"};
        EXPECT_FALSE(parser.parseClientArgs(args, 4, argv));
    }
    ServerArgs missing;
    const char* missingArgv[] = {"server", "--macos-ime-backend"};
    EXPECT_FALSE(parser.parseServerArgs(missing, 2, missingArgv));
    ServerArgs duplicate;
    const char* duplicateArgv[] = {"server", "--macos-ime-backend", "direct-tis",
                                 "--macos-ime-backend", "native-shortcut"};
    EXPECT_FALSE(parser.parseServerArgs(duplicate, 5, duplicateArgv));
}

} // namespace inputleap
