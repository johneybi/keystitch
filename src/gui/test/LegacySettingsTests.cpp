/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "desktop/LegacySettings.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace inputleap {
namespace {
class LegacySettingsTests : public testing::Test {
protected:
    QTemporaryDir dir;
    QSettings previous{dir.filePath("previous.ini"), QSettings::IniFormat};
    QSettings current{dir.filePath("current.ini"), QSettings::IniFormat};
    const QString pending = "_keyStitchPendingLegacyImportV1";
};

TEST_F(LegacySettingsTests, PreservesConnectionExternalConfigPathAndNestedKeys)
{
    previous.setValue("serverHostname", "pair.test");
    previous.setValue("configFile", "C:/Pair settings/한영.conf");
    previous.setValue("useExternalConfig", true);
    previous.beginWriteArray("internalConfig/keyRemaps");
    previous.setArrayIndex(0);
    previous.setValue("output", "F19");
    previous.endArray();
    previous.sync();
    QSettings empty(dir.filePath("empty.ini"), QSettings::IniFormat);
    ASSERT_TRUE(importLegacySettings(current, {&empty, &previous}, dir.path()).isEmpty());
    QSettings reopened(current.fileName(), QSettings::IniFormat);
    EXPECT_EQ(previous.allKeys(), reopened.allKeys());
    for (const auto& key : previous.allKeys()) EXPECT_EQ(previous.value(key), reopened.value(key));
    EXPECT_EQ(1, reopened.beginReadArray("internalConfig/keyRemaps"));
    reopened.setArrayIndex(0);
    EXPECT_EQ(QString("F19"), reopened.value("output").toString());
    reopened.endArray();
    reopened.setValue("serverHostname", "new.test");
    reopened.sync();
    ASSERT_TRUE(importLegacySettings(current, {&previous}, dir.path()).isEmpty());
    EXPECT_EQ(QString("new.test"), current.value("serverHostname").toString());
    EXPECT_EQ(QString("pair.test"), previous.value("serverHostname").toString());
}

TEST_F(LegacySettingsTests, ResumesAfterPartialCopyEvenIfOldSettingsAreGone)
{
    const QVariantMap snapshot{{"port", 24809}, {"serverHostname", "old.test"}};
    current.setValue(pending, snapshot);
    current.setValue("serverHostname", "edited.test");
    current.sync();
    QSettings reopened(current.fileName(), QSettings::IniFormat);
    ASSERT_TRUE(importLegacySettings(reopened, {}, dir.path()).isEmpty());
    EXPECT_EQ(24809, reopened.value("port").toInt());
    EXPECT_EQ(QString("edited.test"), reopened.value("serverHostname").toString());
    EXPECT_FALSE(reopened.contains(pending));
}

TEST_F(LegacySettingsTests, ExistingSettingsAreNeverAugmentedWithLegacyDefaults)
{
    previous.setValue("autoStart", true);
    current.setValue("keyboard/macInputSourceBackend", "native-shortcut");
    ASSERT_TRUE(importLegacySettings(current, {&previous}, dir.path()).isEmpty());
    EXPECT_FALSE(current.contains("autoStart"));
}

TEST_F(LegacySettingsTests, ReadErrorDoesNotInitializeDestination)
{
    QFile file(dir.filePath("broken.ini"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("[broken\nport=24800\n");
    file.close();
    QSettings broken(file.fileName(), QSettings::IniFormat);
    EXPECT_FALSE(importLegacySettings(current, {&broken}, dir.path()).isEmpty());
    EXPECT_TRUE(current.allKeys().isEmpty());
}

TEST_F(LegacySettingsTests, WriteErrorIsReportedWithoutChangingSource)
{
    previous.setValue("screenName", "test-screen");
    QFile file(dir.filePath("not-a-directory"));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.close();
    QSettings unavailable(file.fileName() + "/settings.ini", QSettings::IniFormat);
    EXPECT_FALSE(importLegacySettings(unavailable, {&previous}, dir.path()).isEmpty());
    EXPECT_EQ(QString("test-screen"), previous.value("screenName").toString());
}
} // namespace
} // namespace inputleap
