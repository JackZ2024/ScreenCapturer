#ifndef CAPTURER_H
#define CAPTURER_H

#include "framelesswindow.h"
#include "menu.h"
#include "screenshoter.h"
#include "settingdialog.h"
#include "PinSession.h"
#include "ClosedPinHistory.h"

class PinSessionLifecycle;

#include <memory>
#include <QApplication>
#include <qhotkey.h>
#include <QMimeData>
#include <QPointer>
#include <QScopedPointer>
#include <QSystemTrayIcon>

class Capturer final : public QApplication
{
    Q_OBJECT

public:
    Capturer(int& argc, char **argv);
    ~Capturer();

    void Init();

public slots:
    void QuickLook();

    void PreviewClipboard();
    void RestoreLastClosedPin();
    void ClearClosedPins();
    void PreviewMimeData(const std::shared_ptr<QMimeData>& mimedata);
    void TogglePreviews();
    void TransparentPreviewInput();
    void DisableTransparentPreviewInput();

    void OpenSettingsDialog();
    void UpdateHotkeys();

    void TrayActivated(QSystemTrayIcon::ActivationReason reason);
    void ShowMessage(const QString& title, const QString& msg,
                     QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information, int msecs = 10000);

    void SetTheme(const QString& theme);

    void UpdateScreenshotStyle();

    // void TranslateTxt();

signals:
    void closedPinsAvailableChanged(bool available);

private:
    friend class CapturerExitTest;
    void destroyPreviews();
    void SystemTrayInit();

    void loadBackupImage();
    QList<PinnedImageState> pinnedImages() const;
    void registerPreview(FramelessWindow *preview);
    void restoreClosedPin(quint64 id);
    void rebuildClosedPinsMenu();
    void updateClosedPinActions();
    ClosedPinHistory closed_pins_;
    PinSessionLifecycle *pin_session_lifecycle_{nullptr};
    bool exiting_{false};
    QPointer<QAction> restore_closed_action_;
    QPointer<QMenu> closed_pins_menu_;

    QString theme_{};

    QScopedPointer<QSystemTrayIcon> tray_{};
    QScopedPointer<Menu>            tray_menu_{};
    QPointer<QAction>               tray_snip_{};
    // QPointer<QAction>               tray_record_video_{};
    // QPointer<QAction>               tray_record_gif_{};
    // QPointer<QAction>               tray_open_camera_{};
    QPointer<QAction>               tray_settings_{};
    QPointer<QAction>               tray_exit_{};

    QPointer<SettingWindow> settings_window_{};

    // hotkey
    QPointer<QHotkey> snip_hotkey_{};       // screenshot
    QPointer<QHotkey> restore_closed_hotkey_{};
    QPointer<QHotkey> preview_hotkey_{};    // preview
    QPointer<QHotkey> quicklook_hotkey_{};  // Explorer window, Windows only
    QPointer<QHotkey> disable_transparent_input_{};
    QPointer<QHotkey> transparent_input_{}; // for preview window
    QPointer<QHotkey> toggle_hotkey_{};     // toggle previews
    // QPointer<QHotkey> translate_txt_{};     // toggle previews

    QScopedPointer<ScreenShoter> sniper_{};

    std::list<QPointer<FramelessWindow>> previews_{};
};

inline Capturer *App() { return dynamic_cast<Capturer *>(qApp); }

#endif // CAPTURER_H
