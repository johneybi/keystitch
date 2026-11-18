/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "common/PeerStatus.h"
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--fixture") {
        if (args[2] == "peer-events") {
            using namespace inputleap;
            const auto tag = qgetenv(peerStatusEnv);
            std::puts("connected to server");
            std::printf("%sother-run connected\n", peerStatusPrefix);
            std::fprintf(stderr, "%s%s connected\n", peerStatusPrefix, tag.constData());
            reportPeerStatus(PeerStatus::Listening);
            reportPeerStatus(PeerStatus::Connected);
            reportPeerStatus(PeerStatus::Connected);
            reportPeerStatus(PeerStatus::Disconnected);
            reportPeerStatus(PeerStatus::Connecting);
            reportPeerStatus(PeerStatus::ConnectedEncrypted);
            // A truncated final record must never confirm a connection.
            std::printf("%s%s connected", peerStatusPrefix, tag.constData());
            return 0;
        }
        std::fputs("connec", stdout);
        std::fflush(stdout);
        std::fputs("diagnostic\n", stderr);
        std::fflush(stderr);
        QTimer::singleShot(20, [] {
            std::fputs("ted\n", stdout);
            std::fflush(stdout);
        });
        if (args[2] == "exit") {
            QTimer::singleShot(50, [&app] {
                std::fputs("last line", stderr);
                app.exit(3);
            });
        }
        return app.exec();
    }
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
