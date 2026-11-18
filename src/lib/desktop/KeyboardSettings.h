/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QString>
#include <QStringList>

class QSettings;

namespace inputleap {

enum class DesktopPlatform { MacOS, Windows, Other };

struct KeyboardSettingsSnapshot {
    QString macInputSourceBackend;
    bool isDefault = false;
    QString error;
    bool valid() const { return error.isEmpty(); }
};

// Qt Core only. Widgets and a future QML adapter share this persistence and
// launch contract. Saving a preference does not apply it to a running engine.
class KeyboardSettings {
public:
    explicit KeyboardSettings(QSettings& settings) : settings_(settings) {}
    KeyboardSettingsSnapshot read() const;
    bool saveMacInputSourceBackend(const QString& value, QString& error);
    bool appendLaunchArguments(DesktopPlatform platform, QStringList& args,
                               QString& error) const;
    static constexpr const char* key = "keyboard/macInputSourceBackend";

private:
    QSettings& settings_;
};

} // namespace inputleap
