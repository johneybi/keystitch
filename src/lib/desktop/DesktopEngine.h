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
    Q_PROPERTY(QString confirmedMacInputSourceBackend READ confirmedMacInputSourceBackend NOTIFY macInputSourceBackendChanged)
public:
    enum class State { Stopped, Starting, Running, Retrying, Failed };
    Q_ENUM(State)
    DesktopEngine();
    ~DesktopEngine() override;
    State state() const { return state_; }
    PeerStatus peerStatus() const { return peer_; }
    QString confirmedMacInputSourceBackend() const { return confirmedBackend_; }
    bool active() const;
    void start(const QString& program, const QStringList& arguments);
    void stop();

Q_SIGNALS:
    void stateChanged(State state);
    void peerStatusChanged(PeerStatus status);
    void macInputSourceBackendChanged();
    void outputLine(const QString& line);
    void launchFailed(const QString& error);
    void retryScheduled(int exitCode, int delayMs);

private:
    void resetBackoff();
    void setPeerStatus(PeerStatus status);
    void launch();
    void setConfirmedBackend(const QString& backend);
    void consumeStatus(const QByteArray& value);
    void drain(QProcess* child, bool standardError, bool atEnd);
    void setState(State next);
    State state_ = State::Stopped;
    PeerStatus peer_ = PeerStatus::Unknown;
    QString confirmedBackend_;
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
