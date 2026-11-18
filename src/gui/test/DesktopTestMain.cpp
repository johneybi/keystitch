/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <QCoreApplication>
#include <QTimer>
#include <cstdio>
#include <gtest/gtest.h>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--fixture") {
        std::fputs("connec", stdout);
        std::fflush(stdout);
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
