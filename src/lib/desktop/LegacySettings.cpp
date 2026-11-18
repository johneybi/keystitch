/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "desktop/LegacySettings.h"

#include <QDir>
#include <QLockFile>
#include <QSettings>
#include <QVariant>

namespace inputleap {

namespace {
const QString pendingKey = QStringLiteral("_keyStitchPendingLegacyImportV1");

QString snapshotLegacySettings(const QList<QSettings*>& sources, QVariantMap& values)
{
    for (auto* source : sources) {
        source->setFallbacksEnabled(false);
        const auto keys = source->allKeys();
        for (const auto& key : keys) {
            if (key.section('/', 0, 0).compare(pendingKey, Qt::CaseInsensitive) == 0) {
                return QStringLiteral("Legacy settings contain a reserved migration key.");
            }
            values.insert(key, source->value(key));
        }
        if (source->status() != QSettings::NoError) {
            return QStringLiteral("Could not read the previous application's settings.");
        }
        if (!values.isEmpty()) break;
    }
    return {};
}
} // namespace

QString importLegacySettings(QSettings& destination, const QList<QSettings*>& sources,
                             const QString& stateDirectory)
{
    destination.setFallbacksEnabled(false);
    const auto keys = destination.allKeys();
    if (destination.status() != QSettings::NoError) return QStringLiteral("Could not read application settings.");
    if (!keys.isEmpty() && !destination.contains(pendingKey)) return {};

    if (stateDirectory.isEmpty() || !QDir().mkpath(stateDirectory)) {
        return QStringLiteral("Could not create the settings migration lock directory.");
    }
    QLockFile lock(QDir(stateDirectory).filePath("legacy-settings.lock"));
    if (!lock.tryLock(5000)) return QStringLiteral("Could not lock application settings for migration.");
    destination.sync();
    if (destination.status() != QSettings::NoError) return QStringLiteral("Could not synchronize application settings.");

    QVariantMap values;
    if (destination.contains(pendingKey)) {
        values = destination.value(pendingKey).toMap();
        if (values.isEmpty()) return QStringLiteral("Could not read the pending settings migration.");
    } else {
        // Another launch may have completed the import while we waited for the lock.
        if (!destination.allKeys().isEmpty()) return {};
        const QString error = snapshotLegacySettings(sources, values);
        if (!error.isEmpty() || values.isEmpty()) return error;
        // The Windows registry writes individual values immediately. Persist the
        // complete snapshot before writing the first key so a restart can finish.
        destination.setValue(pendingKey, values);
        destination.sync();
        if (destination.status() != QSettings::NoError) return QStringLiteral("Could not save the settings migration snapshot.");
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!destination.contains(it.key())) destination.setValue(it.key(), it.value());
    }
    destination.sync();
    if (destination.status() != QSettings::NoError) return QStringLiteral("Could not save the imported settings.");
    destination.remove(pendingKey);
    destination.sync();
    if (destination.status() != QSettings::NoError) return QStringLiteral("Could not finalize the settings migration.");
    return {};
}

} // namespace inputleap
