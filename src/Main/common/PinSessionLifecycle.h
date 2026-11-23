#ifndef CAPTURER_PIN_SESSION_LIFECYCLE_H
#define CAPTURER_PIN_SESSION_LIFECYCLE_H

#include "PinSession.h"
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <functional>
#include <optional>

// Saves before system shutdown, while the windows and their images still exist.
class PinSessionLifecycle final : public QObject, public QAbstractNativeEventFilter
{
public:
    using CollectImages = std::function<QList<PinnedImageState>()>;
    using SaveSettings = std::function<void()>;
    PinSessionLifecycle(QString directory, CollectImages collect, SaveSettings saveSettings,
                        QObject *parent = nullptr);
    ~PinSessionLifecycle() override;

    void commitSession();
    void finishSession();
    bool isSystemShutdownPending() const;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QString directory_;
    CollectImages collect_;
    SaveSettings saveSettings_;
    std::optional<QList<PinnedImageState>> checkpoint_;
    bool checkpointSaved_{false};
    bool shutdownConfirmed_{false};
};
#endif
