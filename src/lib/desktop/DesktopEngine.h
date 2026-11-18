/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "common/PeerStatus.h"
#include <QObject>
#include <QStringList>
#include <QTimer>

class QProcess;

namespace inputleap {

// Owned by the application. A reconnect reuses the accepted launch arguments.
class DesktopEngine : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Retrying, Failed };
    Q_ENUM(State)
    DesktopEngine();
    ~DesktopEngine() override;
    State state() const { return state_; }
    PeerStatus peerStatus() const { return peer_; }
    bool active() const;
    void start(const QString& program, const QStringList& arguments);
    void stop();

Q_SIGNALS:
    void stateChanged(State state);
    void peerStatusChanged(PeerStatus status);
    void outputLine(const QString& line);
    void launchFailed(const QString& error);
    void retryScheduled(int exitCode, int delayMs);

private:
    void resetBackoff();
    void setPeerStatus(PeerStatus status);
    void launch();
    void drain(QProcess* child, bool standardError, bool atEnd);
    void setState(State next);
    State state_ = State::Stopped;
    PeerStatus peer_ = PeerStatus::Unknown;
    QByteArray statusPrefix_;
    QProcess* child_ = nullptr;
    QTimer retry_;
    int delayMs_ = 1000;
    QByteArray stdoutTail_;
    QByteArray stderrTail_;
    QString program_;
    QStringList arguments_;
};

} // namespace inputleap
