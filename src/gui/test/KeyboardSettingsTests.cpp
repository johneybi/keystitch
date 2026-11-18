/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "desktop/KeyboardSettings.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace inputleap {

TEST(KeyboardSettingsTests, MissingPreferenceUsesNativeWithoutWriting)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
    KeyboardSettings store(file);
    const auto snapshot = store.read();
    ASSERT_TRUE(snapshot.valid());
    EXPECT_TRUE(snapshot.isDefault);
    EXPECT_EQ(QString("native-shortcut"), snapshot.macInputSourceBackend);
    EXPECT_FALSE(file.contains(KeyboardSettings::key));
    QStringList args{"-f"};
    QString error;
    ASSERT_TRUE(store.appendLaunchArguments(DesktopPlatform::MacOS, args, error));
    EXPECT_EQ((QStringList{"-f", "--macos-ime-backend", "native-shortcut"}), args);
}

TEST(KeyboardSettingsTests, BothModesSurviveReopeningAndPreserveOtherSettings)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const auto& mode : {"direct-tis", "native-shortcut"}) {
        {
            QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
            file.setValue("screenName", "My Mac");
            file.setValue("keyboard/futureOption", 42);
            KeyboardSettings store(file);
            QString error;
            ASSERT_TRUE(store.saveMacInputSourceBackend(mode, error));
            EXPECT_TRUE(error.isEmpty());
        }
        QSettings reopened(dir.filePath("settings.ini"), QSettings::IniFormat);
        KeyboardSettings store(reopened);
        EXPECT_EQ(QString(mode), store.read().macInputSourceBackend);
        EXPECT_FALSE(store.read().isDefault);
        EXPECT_EQ(QString("My Mac"), reopened.value("screenName").toString());
        EXPECT_EQ(42, reopened.value("keyboard/futureOption").toInt());
        QStringList args;
        QString error;
        ASSERT_TRUE(store.appendLaunchArguments(DesktopPlatform::MacOS, args, error));
        EXPECT_EQ(QString(mode), args.last());
    }
}

TEST(KeyboardSettingsTests, InvalidSaveLeavesPreviousValueUntouched)
{
    QTemporaryDir dir;
    QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
    KeyboardSettings store(file);
    QString error;
    ASSERT_TRUE(store.saveMacInputSourceBackend("direct-tis", error));
    for (const auto& value : {"", "auto", "native", " native-shortcut", "DIRECT-TIS"}) {
        EXPECT_FALSE(store.saveMacInputSourceBackend(value, error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(QString("direct-tis"), store.read().macInputSourceBackend);
    }
}

TEST(KeyboardSettingsTests, UnsupportedSavedValueBlocksMacLaunchWithoutFallback)
{
    QTemporaryDir dir;
    QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
    file.setValue(KeyboardSettings::key, "future-method");
    KeyboardSettings store(file);
    EXPECT_FALSE(store.read().valid());
    QStringList args{"existing"};
    QString error;
    EXPECT_FALSE(store.appendLaunchArguments(DesktopPlatform::MacOS, args, error));
    EXPECT_EQ((QStringList{"existing"}), args);
    EXPECT_EQ(QString("future-method"), file.value(KeyboardSettings::key).toString());
    ASSERT_TRUE(store.saveMacInputSourceBackend("native-shortcut", error));
    EXPECT_TRUE(store.read().valid());
}

TEST(KeyboardSettingsTests, WindowsAndOtherPlatformsNeverReceiveMacFlag)
{
    QTemporaryDir dir;
    QSettings file(dir.filePath("settings.ini"), QSettings::IniFormat);
    file.setValue(KeyboardSettings::key, "unknown-mac-only-value");
    KeyboardSettings store(file);
    for (const auto platform : {DesktopPlatform::Windows, DesktopPlatform::Other}) {
        QStringList args{"-f"};
        QString error;
        EXPECT_TRUE(store.appendLaunchArguments(platform, args, error));
        EXPECT_EQ((QStringList{"-f"}), args);
        EXPECT_TRUE(error.isEmpty());
    }
}

TEST(KeyboardSettingsTests, UnwritableStorageDoesNotReportSaveSuccess)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QFile blocker(dir.filePath("not-a-directory"));
    ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    QSettings file(blocker.fileName() + "/settings.ini", QSettings::IniFormat);
    KeyboardSettings store(file);
    QString error;
    EXPECT_FALSE(store.saveMacInputSourceBackend("native-shortcut", error));
    EXPECT_FALSE(error.isEmpty());
    EXPECT_FALSE(file.contains(KeyboardSettings::key));
    QStringList args;
    EXPECT_FALSE(store.appendLaunchArguments(DesktopPlatform::MacOS, args, error));
    EXPECT_TRUE(args.isEmpty());
}

} // namespace inputleap
