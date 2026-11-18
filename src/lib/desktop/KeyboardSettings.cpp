/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "KeyboardSettings.h"
#include "base/MacInputSourceBackend.h"

#include <QSettings>
#include <QVariant>

namespace inputleap {

KeyboardSettingsSnapshot KeyboardSettings::read() const
{
    KeyboardSettingsSnapshot result;
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        result.error = QStringLiteral("Cannot read keyboard settings.");
        return result;
    }
    result.isDefault = !settings_.contains(key);
    result.macInputSourceBackend = settings_.value(key, "native-shortcut").toString();
    if (parseMacInputSourceBackend(result.macInputSourceBackend.toStdString()) ==
        MacInputSourceBackend::Invalid) {
        result.error = QStringLiteral("Unsupported saved input source method. Choose a supported method in Settings.");
    }
    return result;
}

bool KeyboardSettings::saveMacInputSourceBackend(const QString& value, QString& error)
{
    error.clear();
    if (parseMacInputSourceBackend(value.toStdString()) == MacInputSourceBackend::Invalid) {
        error = QStringLiteral("Unsupported input source method.");
        return false;
    }
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        error = QStringLiteral("Cannot access keyboard settings.");
        return false;
    }
    const auto previous = settings_.value(key);
    settings_.setValue(key, value);
    settings_.sync();
    if (settings_.status() != QSettings::NoError) {
        // Restore the in-process value as well: never report an uncommitted
        // selection as saved. Keep QSettings' atomic-sync requirement enabled.
        if (previous.isValid()) settings_.setValue(key, previous);
        else settings_.remove(key);
        error = QStringLiteral("Could not save keyboard settings. The method was not applied.");
        return false;
    }
    return true;
}

bool KeyboardSettings::appendLaunchArguments(DesktopPlatform platform, QStringList& args,
                                              QString& error) const
{
    error.clear();
    if (platform != DesktopPlatform::MacOS) return true;
    const auto snapshot = read();
    if (!snapshot.valid()) {
        error = snapshot.error;
        return false;
    }
    args << "--macos-ime-backend" << snapshot.macInputSourceBackend;
    return true;
}

} // namespace inputleap
