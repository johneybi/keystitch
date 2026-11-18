/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "desktop/DesktopEngine.h"
#include "desktop/KeyboardSettings.h"
#include <QSettings>
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
    // The relative order of the two OS pipes is unspecified.
    EXPECT_EQ(6, lines.size());
    EXPECT_EQ(2, lines.count("diagnostic"));
    EXPECT_EQ(2, lines.count("connected"));
    EXPECT_EQ(2, lines.count("last line"));
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
TEST(DesktopEngineTests, OnlyCompleteCurrentChildStatusRecordsChangePeerState)
{
    DesktopEngine engine;
    QList<PeerStatus> observed;
    QObject::connect(&engine, &DesktopEngine::peerStatusChanged,
                     [&](PeerStatus status) { observed << status; });
    engine.start(QCoreApplication::applicationFilePath(), {"--fixture", "peer-events"});
    ASSERT_TRUE(waitUntil([&] { return engine.state() == DesktopEngine::State::Retrying; }));
    EXPECT_EQ((QList<PeerStatus>{PeerStatus::Listening, PeerStatus::Connected,
                                PeerStatus::Disconnected, PeerStatus::Connecting,
                                PeerStatus::ConnectedEncrypted, PeerStatus::Unknown}), observed);
    EXPECT_EQ(PeerStatus::Unknown, engine.peerStatus());
    engine.stop();
}
TEST(DesktopEngineTests, ConfirmedMethodTracksChildAcrossSaveRetryAndManualRestart)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
    KeyboardSettings store(file);
    DesktopEngine engine;
    QStringList reports;
    QObject::connect(&engine, &DesktopEngine::macInputSourceBackendChanged, [&] {
        reports << engine.confirmedMacInputSourceBackend();
    });
    QString error;
    QStringList first{"--fixture-ime", "exit"};
    ASSERT_TRUE(store.appendLaunchArguments(DesktopPlatform::MacOS, first, error));
    const auto program = QCoreApplication::applicationFilePath();
    engine.start(program, first);
    ASSERT_TRUE(waitUntil([&] { return reports.contains("native-shortcut"); }));
    ASSERT_TRUE(store.saveMacInputSourceBackend("direct-tis", error));
    ASSERT_TRUE(waitUntil([&] { return reports.count("native-shortcut") == 2; }));
    engine.stop();
    EXPECT_TRUE(engine.confirmedMacInputSourceBackend().isEmpty());
    EXPECT_FALSE(reports.contains("direct-tis"));

    QStringList next{"--fixture-ime", "hold"};
    ASSERT_TRUE(store.appendLaunchArguments(DesktopPlatform::MacOS, next, error));
    engine.start(program, next);
    ASSERT_TRUE(waitUntil([&] { return engine.confirmedMacInputSourceBackend() == "direct-tis"; }));
    engine.stop();
    EXPECT_TRUE(engine.confirmedMacInputSourceBackend().isEmpty());
}

} // namespace inputleap
