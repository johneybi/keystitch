/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "DesktopEngine.h"
#include <QProcess>

namespace inputleap {

DesktopEngine::DesktopEngine()
{
    retry_.setSingleShot(true);
    connect(&retry_, &QTimer::timeout, this, [this] {
        if (state_ == State::Retrying) launch();
    });
}

DesktopEngine::~DesktopEngine()
{
    blockSignals(true);
    stop();
}

bool DesktopEngine::active() const
{
    return state_ == State::Starting || state_ == State::Running || state_ == State::Retrying;
}

void DesktopEngine::setState(State next)
{
    if (state_ == next) return;
    state_ = next;
    Q_EMIT stateChanged(next);
}

void DesktopEngine::start(const QString& program, const QStringList& arguments)
{
    if (active()) return;
    program_ = program;
    arguments_ = arguments;
    resetBackoff();
    launch();
}

void DesktopEngine::resetBackoff()
{
    delayMs_ = 1000;
}

void DesktopEngine::drain(QProcess* child, bool atEnd)
{
    // Preserve incomplete lines (including split UTF-8 characters) until the
    // next read. A log consumer may stop or replace the child synchronously.
    while (child_ == child && (child->canReadLine() || (atEnd && child->bytesAvailable()))) {
        auto bytes = child->readLine();
        if (bytes.endsWith('\n')) bytes.chop(1);
        if (bytes.endsWith('\r')) bytes.chop(1);
        Q_EMIT outputLine(QString::fromUtf8(bytes));
    }
}

void DesktopEngine::launch()
{
    auto* child = new QProcess(this);
    child_ = child;
    child->setProcessChannelMode(QProcess::MergedChannels);
    connect(child, &QProcess::started, this, [this, child] {
        if (child_ == child) setState(State::Running);
    });
    connect(child, &QProcess::readyRead, this, [this, child] { drain(child, false); });
    connect(child, &QProcess::errorOccurred, this, [this, child](QProcess::ProcessError error) {
        if (child != child_ || error != QProcess::FailedToStart) return;
        const QString message = child->errorString();
        child_ = nullptr;
        child->deleteLater();
        setState(State::Failed);
        if (state_ == State::Failed) Q_EMIT launchFailed(message);
    });
    connect(child, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, child](int exitCode) {
        drain(child, true);
        if (child != child_) return;
        child_ = nullptr;
        child->deleteLater();
        const int delay = delayMs_;
        delayMs_ = qMin(delay * 2, 30000);
        retry_.start(delay);
        setState(State::Retrying);
        if (state_ == State::Retrying) Q_EMIT retryScheduled(exitCode, delay);
    });
    setState(State::Starting);
    if (child_ == child) child->start(program_, arguments_);
}

void DesktopEngine::stop()
{
    retry_.stop();
    auto* child = child_;
    child_ = nullptr;
    if (child) {
        disconnect(child, nullptr, this, nullptr);
        if (child->state() != QProcess::NotRunning) {
#ifdef Q_OS_UNIX
            child->terminate();
            if (!child->waitForFinished(5000))
#endif
            {
                child->kill();
                child->waitForFinished();
            }
        }
        child->deleteLater();
    }
    setState(State::Stopped);
}

} // namespace inputleap
