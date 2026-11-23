#include "PinSessionLifecycle.h"
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

PinSessionLifecycle::PinSessionLifecycle(QString directory, CollectImages collect,
                                         SaveSettings saveSettings, QObject *parent)
    : QObject(parent), directory_(std::move(directory)), collect_(std::move(collect)),
      saveSettings_(std::move(saveSettings))
{
    auto *app = QCoreApplication::instance();
    app->installNativeEventFilter(this);
#ifndef QT_NO_SESSIONMANAGER
    if (auto *gui = qobject_cast<QGuiApplication *>(app)) {
        // A queued callback may never run once Windows ends the session.
        connect(gui, &QGuiApplication::commitDataRequest, this,
                [this](QSessionManager &) { commitSession(); }, Qt::DirectConnection);
    }
#endif
    connect(app, &QCoreApplication::aboutToQuit, this,
            [this] { finishSession(); }, Qt::DirectConnection);
}

PinSessionLifecycle::~PinSessionLifecycle()
{
    if (auto *app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

void PinSessionLifecycle::commitSession()
{
    checkpoint_ = collect_();
    checkpointSaved_ = pin_session::save(directory_, *checkpoint_);
    if (!checkpointSaved_) qWarning() << "Could not save pinned images before system shutdown.";
    saveSettings_();
    // Do not close windows here. The shutdown request may still be cancelled.
}

void PinSessionLifecycle::finishSession()
{
    if (shutdownConfirmed_ && checkpoint_) {
        // System teardown may already have emptied the live window list. Never
        // overwrite the committed snapshot with that list. Retry a failed save
        // with the same snapshot while Qt's last shutdown callback is available.
        if (!checkpointSaved_) checkpointSaved_ = pin_session::save(directory_, *checkpoint_);
    } else {
        // A normal exit (including one after a cancelled shutdown) saves live state.
        if (!pin_session::save(directory_, collect_()))
            qWarning() << "Pinned image session was not saved; previous recovery files were retained.";
    }
    saveSettings_();
}

bool PinSessionLifecycle::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (message && (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG")) {
        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message == WM_ENDSESSION) {
            // Qt emits aboutToQuit while processing the successful WM_ENDSESSION.
            // Observe the outcome first, and let Qt continue processing the message.
            shutdownConfirmed_ = msg->wParam != FALSE;
            if (!shutdownConfirmed_) {
                checkpoint_.reset();
                checkpointSaved_ = false;
            }
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}

bool PinSessionLifecycle::isSystemShutdownPending() const
{
#ifdef Q_OS_WIN
    return checkpoint_.has_value() || shutdownConfirmed_;
#elif !defined(QT_NO_SESSIONMANAGER)
    const auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    return app && app->isSavingSession();
#else
    return false;
#endif
}
