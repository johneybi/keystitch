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

void DesktopEngine::drain(QProcess* child, bool standardError, bool atEnd)
{
    if (child != child_) return;
    auto& tail = standardError ? stderrTail_ : stdoutTail_;
    tail += standardError ? child->readAllStandardError() : child->readAllStandardOutput();
    while (child == child_ && !tail.isEmpty()) {
        const auto newline = tail.indexOf('\n');
        if (newline < 0 && !atEnd) return;
        const auto length = newline < 0 ? tail.size() : newline;
        auto bytes = tail.left(length);
        tail.remove(0, newline < 0 ? length : length + 1);
        if (bytes.endsWith('\r')) bytes.chop(1);
        Q_EMIT outputLine(QString::fromUtf8(bytes));
    }
}

void DesktopEngine::launch()
{
    auto* child = new QProcess(this);
    child_ = child;
    stdoutTail_.clear();
    stderrTail_.clear();
    connect(child, &QProcess::started, this, [this, child] {
        if (child_ == child) setState(State::Running);
    });
    connect(child, &QProcess::readyReadStandardOutput, this, [this, child] { drain(child, false, false); });
    connect(child, &QProcess::readyReadStandardError, this, [this, child] { drain(child, true, false); });
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
        drain(child, false, true);
        drain(child, true, true);
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
