#include "Capturer.h"

#include "clipboard.h"
#include "ColorWindow.h"
#include "GlobalSettings.h"
#include "PinSession.h"
#include "PinSessionLifecycle.h"
#include "ImageWindow.h"
#include "logging.h"
#include "settingdialog.h"

#ifdef Q_OS_MAC
#include "MacHelper.h"
#endif

#include <QApplication>
#include <QFileInfo>
#include <QKeyEvent>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QUrl>
#include <QStyleFactory>
#include <QDir>
#include <vector>
#include <utility>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define SET_HOTKEY(X, Y)                                                                                   \
    if (!X->setShortcut(Y, true)) {                                                                        \
        logw("Failed to register hotkey : {}", Y.toString().toStdString());                                \
        error += tr("Failed to register hotkey : <%1>\n").arg(Y.toString());                               \
    }

Capturer::Capturer(int& argc, char **argv)
    : QApplication(argc, argv)
{
    setWindowIcon(QIcon(":/icons/logo"));
    setStyle(QStyleFactory::create("Fusion"));
// #ifdef Q_OS_MAC
//     setStyle(QStyleFactory::create("macOS"));
// #else
//     setStyle(QStyleFactory::create("Fusion"));
// #endif

    snip_hotkey_       = new QHotkey(this);
    preview_hotkey_    = new QHotkey(this);
    restore_closed_hotkey_ = new QHotkey(this);
    toggle_hotkey_     = new QHotkey(this);
    quicklook_hotkey_  = new QHotkey(this);
    transparent_input_ = new QHotkey(this);
    disable_transparent_input_ = new QHotkey(this);
    // translate_txt_ = new QHotkey(this);

    sniper_.reset(new ScreenShoter());

    connect(snip_hotkey_, &QHotkey::activated, sniper_.get(), &ScreenShoter::start);
    connect(preview_hotkey_, &QHotkey::activated, this, &Capturer::PreviewClipboard);
    connect(restore_closed_hotkey_, &QHotkey::activated, this, &Capturer::RestoreLastClosedPin);
    connect(toggle_hotkey_, &QHotkey::activated, this, &Capturer::TogglePreviews);
    connect(quicklook_hotkey_, &QHotkey::activated, this, &Capturer::QuickLook);
    connect(transparent_input_, &QHotkey::activated, this, &Capturer::TransparentPreviewInput);
    connect(disable_transparent_input_, &QHotkey::activated, this, &Capturer::DisableTransparentPreviewInput);
    connect(sniper_.get(), &ScreenShoter::pinData, this, &Capturer::PreviewMimeData);
    // connect(translate_txt_, &QHotkey::activated, this, &Capturer::TranslateTxt);
}

Capturer::~Capturer()
{
    exiting_ = true;
    destroyPreviews();
    // The filter and its collect callback refer to Capturer's members. Remove
    // them before those members and QApplication's native windows are destroyed.
    delete pin_session_lifecycle_;
    pin_session_lifecycle_ = nullptr;
}

void Capturer::destroyPreviews()
{
    // close() with WA_DeleteOnClose only posts a DeferredDelete. At application
    // exit there may be no further event loop turn to destroy these windows.
    const auto windows = std::move(previews_);
    previews_.clear();
    for (const auto &preview : windows) {
        if (!preview) continue;
        disconnect(preview, nullptr, this, nullptr);
        disconnect(this, nullptr, preview, nullptr);
        preview->setAttribute(Qt::WA_DeleteOnClose, false);
        delete preview.data();
    }
}

void Capturer::Init()
{
    clipboard::init();

    closed_pins_.setLimit(g_Settings->closedPinsLimit);
    connect(g_Settings, &GlobalSettings::closedPinsLimitChanged, this, [this](int limit) {
        closed_pins_.setLimit(limit);
        updateClosedPinActions();
    });
    SystemTrayInit();

    UpdateHotkeys();

    UpdateScreenshotStyle();

    SetTheme(g_Settings->definiteTheme());

    connect(g_Settings, &GlobalSettings::onThemeChanged, this, &Capturer::SetTheme);
    g_Settings->monitorSystemTheme(g_Settings->theme == "auto");

    g_Settings->setAutorun(g_Settings->autorun);

    connect(this, &QCoreApplication::aboutToQuit, this, [this] {
        exiting_ = true;
        closed_pins_.clear();
    });
    pin_session_lifecycle_ = new PinSessionLifecycle(pin_session::directory(), [this] { return pinnedImages(); },
                            [] { g_Settings->save(); }, this);
    // Registered after the lifecycle's direct aboutToQuit handler: save all
    // live pins first, then release their widgets while QApplication is intact.
    connect(this, &QCoreApplication::aboutToQuit, this, &Capturer::destroyPreviews, Qt::DirectConnection);
    loadBackupImage();
}

void Capturer::SystemTrayInit()
{
    tray_.reset(new QSystemTrayIcon(QIcon(":/icons/logo"), this));
    tray_->setToolTip("Capturer Settings");

    tray_menu_.reset(new Menu());
    tray_menu_->setObjectName("tray-menu");

    const QString color = (g_Settings->definiteTheme() == "dark") ? "light" : "dark";

    tray_snip_         = new QAction(QIcon(":/icons/screenshot-" + color), tr("Screenshot"));
    tray_settings_     = new QAction(QIcon(":/icons/setting-" + color), tr("Settings"));
    tray_exit_         = new QAction(QIcon(":/icons/exit-" + color), tr("Quit"));

    tray_menu_->addAction(tray_snip_);
    tray_menu_->addSeparator();
    restore_closed_action_ = tray_menu_->addAction(tr("Restore Last Closed Pin"), this, &Capturer::RestoreLastClosedPin);
    closed_pins_menu_ = tray_menu_->addMenu(tr("Recently Closed Pins"));
    connect(closed_pins_menu_, &QMenu::aboutToShow, this, &Capturer::rebuildClosedPinsMenu);
    updateClosedPinActions();
    tray_menu_->addSeparator();
    tray_menu_->addAction(tray_settings_);
    tray_menu_->addAction(tr("Disable All Click-through"), this, &Capturer::DisableTransparentPreviewInput);
    tray_menu_->addSeparator();
    tray_menu_->addAction(tray_exit_);

    tray_->setContextMenu(tray_menu_.data());
    tray_->show();

    connect(tray_.data(), &QSystemTrayIcon::activated, this, &Capturer::TrayActivated);
    connect(tray_snip_, &QAction::triggered, sniper_.data(), &ScreenShoter::start);
    connect(tray_settings_, &QAction::triggered, this, &Capturer::OpenSettingsDialog);
    connect(tray_exit_, &QAction::triggered, qApp, &QCoreApplication::exit);
}

QList<PinnedImageState> Capturer::pinnedImages() const
{
    QList<PinnedImageState> images;
    for (const auto &preview : previews_) {
        if (const auto *image = qobject_cast<ImageWindow *>(preview.data())) {
            auto state = image->sessionState();
            if (!state.image.isNull()) images.push_back(state);
        }
    }
    return images;
}

void Capturer::loadBackupImage()
{
    const auto directory = pin_session::directory();
    const auto images = QFile::exists(QDir(directory).filePath("session.json"))
        ? pin_session::load(directory)
        : pin_session::loadLegacy(QCoreApplication::applicationDirPath() + "/capture");
    for (const auto &state : images) {
        if (auto *preview = ImageWindow::fromSession(state)) registerPreview(preview);
    }
}

void Capturer::registerPreview(FramelessWindow *preview)
{
    preview->setAttribute(Qt::WA_DeleteOnClose);
    previews_.push_back(preview);
    connect(preview, &FramelessWindow::closed, this, [this, preview] {
        const auto found = std::find_if(previews_.begin(), previews_.end(),
                                        [preview](const auto &entry) { return entry.data() == preview; });
        if (found == previews_.end()) return;
        if (!exiting_ && !(pin_session_lifecycle_ && pin_session_lifecycle_->isSystemShutdownPending())) {
            if (const auto *image = qobject_cast<ImageWindow *>(preview)) closed_pins_.remember(image->sessionState());
        }
        // Remove immediately: WA_DeleteOnClose defers destruction and QPointer clearing.
        previews_.remove_if([preview](const auto &entry) { return !entry || entry.data() == preview; });
        updateClosedPinActions();
    });
    connect(preview, &FramelessWindow::closeAll, this, [this] {
        const auto windows = previews_;
        for (const auto &window : windows) if (window) window->closeSelf();
    });
    if (auto *image = qobject_cast<ImageWindow *>(preview)) {
        image->setClosedPinRestoreAvailable(!closed_pins_.empty());
        connect(image, &ImageWindow::restoreClosedPinRequested, this, &Capturer::RestoreLastClosedPin);
        connect(this, &Capturer::closedPinsAvailableChanged, image, &ImageWindow::setClosedPinRestoreAvailable);
    }
    preview->show();
#ifdef Q_OS_APPLE
    onTopWindow(preview->winId());
#endif
}

void Capturer::updateClosedPinActions()
{
    const bool available = !closed_pins_.empty();
    if (restore_closed_action_) restore_closed_action_->setEnabled(available);
    if (closed_pins_menu_) closed_pins_menu_->setEnabled(available);
    emit closedPinsAvailableChanged(available);
}

void Capturer::RestoreLastClosedPin()
{
    restoreClosedPin(closed_pins_.lastId());
}

void Capturer::restoreClosedPin(quint64 id)
{
    if (exiting_ || (pin_session_lifecycle_ && pin_session_lifecycle_->isSystemShutdownPending())) return;
    const auto closed = closed_pins_.entry(id);
    if (!closed) return;
    auto *window = ImageWindow::fromSession(closed->state);
    if (!window) return; // Keep the entry available if restoration failed.
    closed_pins_.take(id);
    updateClosedPinActions();
    registerPreview(window);
    window->raise();
    window->activateWindow();
}

void Capturer::ClearClosedPins()
{
    closed_pins_.clear();
    updateClosedPinActions();
}

void Capturer::rebuildClosedPinsMenu()
{
    closed_pins_menu_->clear();
    int index = 1;
    for (auto it = closed_pins_.entries().crbegin(); it != closed_pins_.entries().crend(); ++it) {
        const auto &closed = *it;
        const auto icon = QIcon(QPixmap::fromImage(closed.state.image).scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        const auto label = tr("%1. %2 x %3 (%4)").arg(index++).arg(closed.state.image.width())
                           .arg(closed.state.image.height()).arg(closed.closedAt.toString("HH:mm:ss"));
        closed_pins_menu_->addAction(icon, label, this, [this, id = closed.id] { restoreClosedPin(id); });
    }
    closed_pins_menu_->addSeparator();
    closed_pins_menu_->addAction(tr("Clear Recently Closed Pins"), this, &Capturer::ClearClosedPins);
}

void Capturer::PreviewClipboard() { PreviewMimeData(clipboard::back(true)); }

void Capturer::PreviewMimeData(const std::shared_ptr<QMimeData>& mimedata)
{
    if (mimedata != nullptr) {

        FramelessWindow *preview{};

        if (mimedata->hasUrls() && mimedata->urls().size() == 1 && mimedata->urls()[0].isLocalFile() &&
            QFileInfo(mimedata->urls()[0].toLocalFile()).isFile() &&
            QString("gif;mp4;mkv;m2ts;mts;avi;wmv;ts;mov;flv")
                .split(';')
                .contains(QFileInfo(mimedata->urls()[0].fileName()).suffix(), Qt::CaseInsensitive)) {

            // preview = new VideoPlayer();
            // dynamic_cast<VideoPlayer *>(preview)->open(mimedata->urls()[0].toLocalFile().toStdString());
            // dynamic_cast<VideoPlayer *>(preview)->start();

            // mimedata->setData(clipboard::MIME_TYPE_STATUS, "P");
            return;
        }
        else if (mimedata->hasColor()) {
            preview = new ColorWindow(mimedata);
        }
        else {
            preview = new ImageWindow(mimedata);
        }

        if (!mimedata->hasFormat(clipboard::MIME_TYPE_POINT)) {
            const auto screen = screenAt(QCursor::pos()) ? screenAt(QCursor::pos()) : primaryScreen();
            preview->move(screen->geometry().center() - preview->rect().center());
        }
        registerPreview(preview);
    }
}

void Capturer::OpenSettingsDialog()
{
    if (settings_window_) {
        // settings_window_->activateWindow();
        // settings_window_->raise();
        settings_window_->show();
        settings_window_->raise();
#ifdef Q_OS_MAC
        activateApp();
#endif
        settings_window_->activateWindow();
        return;
    }

    settings_window_ = new SettingWindow();
    settings_window_->show();

    settings_window_->raise();
#ifdef Q_OS_MAC
    activateApp();
#endif
    settings_window_->activateWindow();

    settings_window_->move(primaryScreen()->geometry().center() -
                           QPoint{ settings_window_->width() / 2, settings_window_->height() / 2 });
}

void Capturer::QuickLook()
{
#if _WIN32
    const auto actived = probe::graphics::active_window();
    if (!actived || actived->pname != "explorer.exe") return;

    const auto file = probe::graphics::explorer_focused(actived->handle);

    if (file.empty()) return;

    auto data = std::make_shared<QMimeData>();
    data->setUrls({ QUrl::fromLocalFile(QString::fromUtf8(file.c_str())) });

    PreviewMimeData(data);
#endif
}

void Capturer::TogglePreviews()
{
    if (previews_.empty()) return;

    bool visible = false; // current state
    for (const auto& win : previews_) {
        if (win && win->isVisible()) {
            visible = true;
            break;
        }
    }

    for (const auto& win : previews_) {
        if (win) {
            win->setVisible(!visible);
        }
    }
}

void Capturer::DisableTransparentPreviewInput()
{
    for (const auto &window : previews_) if (window) window->setTransparentInput(false);
}

void Capturer::TransparentPreviewInput()
{
    const QPoint cursor = QCursor::pos();
    FramelessWindow *target = nullptr;
#ifdef Q_OS_WIN
    // Use native Z order: overlapping pins must not all toggle at once. A
    // click-through window is still a candidate even though WindowFromPoint skips it.
    std::vector<HWND> order;
    EnumWindows([](HWND handle, LPARAM parameter) -> BOOL {
        reinterpret_cast<std::vector<HWND> *>(parameter)->push_back(handle);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&order));
    for (const HWND handle : order) {
        if (target) break;
        for (const auto &window : previews_) {
            if (window && window->isVisible() && window->geometry().contains(cursor)
                && reinterpret_cast<HWND>(window->winId()) == handle) {
                target = window;
                break;
            }
        }
    }
#else
    if (auto *at = QApplication::topLevelAt(cursor)) {
        for (const auto &window : previews_) if (window.data() == at) target = window;
    }
    if (!target) {
        for (auto it = previews_.rbegin(); it != previews_.rend(); ++it)
            if (*it && (*it)->isVisible() && (*it)->geometry().contains(cursor)) { target = *it; break; }
    }
#endif
    if (target) target->toggleTransparentInput();
    else DisableTransparentPreviewInput();
}

void Capturer::UpdateHotkeys()
{
    QString error = "";
    // clang-format off
    SET_HOTKEY(snip_hotkey_,        g_Settings->screenshot);
    SET_HOTKEY(preview_hotkey_,     g_Settings->preview);
    SET_HOTKEY(restore_closed_hotkey_, g_Settings->restore_closed_pin);
    SET_HOTKEY(toggle_hotkey_,      g_Settings->toggle_previews);
#if _WIN32
    SET_HOTKEY(quicklook_hotkey_,   g_Settings->quick_look);
    // SET_HOTKEY(translate_txt_,   g_Settings->translate_txt);
#endif
    SET_HOTKEY(transparent_input_,  g_Settings->transparent_input);
    SET_HOTKEY(disable_transparent_input_, g_Settings->disable_transparent_input);
    // clang-format on
    if (!error.isEmpty()) ShowMessage("Capturer", error, QSystemTrayIcon::Critical);
}

void Capturer::TrayActivated(const QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::DoubleClick && sniper_) sniper_->start();
}

void Capturer::ShowMessage(const QString& title, const QString& msg,
                           const QSystemTrayIcon::MessageIcon icon, const int msecs)
{
    tray_->showMessage(title, msg, icon, msecs);
}

void Capturer::SetTheme(const QString& theme)
{
    if (theme_ == theme || (theme != "dark" && theme != "light")) return;

    theme_ = theme;

    QString baseStyle = ":/stylesheets/capturer";

#ifdef Q_OS_MAC
    baseStyle = ":/stylesheets/capturer-mac";
#endif


    std::vector<QString> files{
        baseStyle,
        ":/stylesheets/capturer-" + theme,
        ":/stylesheets/menu",
        ":/stylesheets/menu-" + theme,
        ":/stylesheets/settingswindow",
        ":/stylesheets/settingswindow-" + theme,
        ":/stylesheets/player",
    };

    QString style{};
    for (auto& qss : files) {

        QFile file(qss);
        file.open(QFile::ReadOnly);

        if (file.isOpen()) {
            style += file.readAll();
            file.close();
        }
    }
    setStyleSheet(style);

    // system tray menu icons
    const QString color = (g_Settings->definiteTheme() == "dark") ? "light" : "dark";

    tray_snip_->setIcon(QIcon(":/icons/screenshot-" + color));
    tray_settings_->setIcon(QIcon(":/icons/setting-" + color));
    tray_exit_->setIcon(QIcon(":/icons/exit-" + color));
}

void Capturer::UpdateScreenshotStyle()
{
    if (sniper_) sniper_->setStyle(g_Settings->style);
}

// void simulateSystemCtrlC() {
//     keybd_event(VK_CONTROL, 0, 0, 0);       // 按下 Ctrl
//     keybd_event('C', 0, 0, 0);              // 按下 C
//     keybd_event('C', 0, KEYEVENTF_KEYUP, 0); // 释放 C
//     keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0); // 释放 Ctrl
// }

// void simulateCtrlC() {
//     // 输入事件数组
//     INPUT inputs[4] = {};
//     ZeroMemory(inputs, sizeof(inputs));

//     // 按下 Ctrl 键
//     inputs[0].type = INPUT_KEYBOARD;
//     inputs[0].ki.wVk = VK_CONTROL;

//     // 按下 C 键
//     inputs[1].type = INPUT_KEYBOARD;
//     inputs[1].ki.wVk = 'C';

//     // 释放 C 键
//     inputs[2].type = INPUT_KEYBOARD;
//     inputs[2].ki.wVk = 'C';
//     inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;

//     // 释放 Ctrl 键
//     inputs[3].type = INPUT_KEYBOARD;
//     inputs[3].ki.wVk = VK_CONTROL;
//     inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;

//     // 发送输入事件
//     SendInput(4, inputs, sizeof(INPUT));
// }

// void SendCopyMessageToActiveWindow() {
//     HWND hwnd = GetForegroundWindow(); // 获取当前激活窗口
//     if (hwnd == nullptr) {
//         qDebug() << "无法获取当前激活窗口句柄";
//         return;
//     }

//     simulateSystemCtrlC();

//     // 发送 WM_COPY 消息
//     // SendMessage(hwnd, WM_COPY, 0, 0);
//     qDebug() << "已向窗口发送 WM_COPY 消息";
// }

// void Capturer::TranslateTxt()
// {
//     SendCopyMessageToActiveWindow();
// }
