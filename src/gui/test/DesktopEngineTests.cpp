/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "desktop/DesktopEngine.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace inputleap {
namespace {
template<class Predicate>
bool waitUntil(Predicate condition, int timeoutMs = 5000)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!condition() && elapsed.elapsed() < timeoutMs) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return condition();
}
}

TEST(DesktopEngineTests, RetryUsesOriginalArgumentsAndStopCancelsPendingLaunch)
{
    DesktopEngine engine;
    QStringList lines;
    QList<int> delays;
    int starts = 0;
    QObject::connect(&engine, &DesktopEngine::stateChanged, [&](DesktopEngine::State state) {
        if (state == DesktopEngine::State::Starting) ++starts;
    });
    QObject::connect(&engine, &DesktopEngine::outputLine, [&](const QString& line) {
        lines << line;
    });
    QObject::connect(&engine, &DesktopEngine::retryScheduled, [&](int code, int delay) {
        EXPECT_EQ(3, code);
        delays << delay;
    });
    const auto program = QCoreApplication::applicationFilePath();
    engine.start(program, {"--fixture", "exit"});
    engine.start(program, {"--fixture", "hold"});
    ASSERT_TRUE(waitUntil([&] { return delays.size() == 1; }));
    engine.start(program, {"--fixture", "hold"});
    ASSERT_TRUE(waitUntil([&] { return delays.size() == 2; }));
    EXPECT_EQ((QStringList{"connected", "last line", "connected", "last line"}), lines);
    EXPECT_EQ((QList<int>{1000, 2000}), delays);
    engine.stop();
    EXPECT_FALSE(waitUntil([&] { return starts > 2; }, 2200));
    EXPECT_EQ(DesktopEngine::State::Stopped, engine.state());
}

TEST(DesktopEngineTests, FailedLaunchCanBeReplacedAndRunningChildCanBeStopped)
{
    DesktopEngine engine;
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString error;
    QObject::connect(&engine, &DesktopEngine::launchFailed, [&](const QString& msg) { error = msg; });
    engine.start(dir.filePath("missing"), {});
    ASSERT_TRUE(waitUntil([&] { return !error.isEmpty(); }));
    EXPECT_EQ(DesktopEngine::State::Failed, engine.state());
    engine.start(QCoreApplication::applicationFilePath(), {"--fixture", "hold"});
    ASSERT_TRUE(waitUntil([&] { return engine.state() == DesktopEngine::State::Running; }));
    engine.stop();
    EXPECT_EQ(DesktopEngine::State::Stopped, engine.state());
}
} // namespace inputleap
