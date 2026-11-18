/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include <QList>
#include <QString>

class QSettings;

namespace inputleap {

// Uses dedicated root-level settings objects. Sources are listed in preference order.
// An empty result allows normal startup. On error the caller must not save defaults.
QString importLegacySettings(QSettings& destination, const QList<QSettings*>& sources,
                             const QString& stateDirectory);

} // namespace inputleap
