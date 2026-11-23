#include "GlobalSettings.h"
#include "logging.h"
#include <probe/system.h>
#include <probe/util.h>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStandardPaths>
#include <cmath>
#include <filesystem>
#ifdef Q_OS_MAC
#include "MacHelper.h"
#endif
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#endif

GlobalSettings *GlobalSettings::instance()
{
    // The application owns the settings; create them only after QApplication exists.
    static GlobalSettings *settings = new GlobalSettings(qApp,
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
    return settings;
}

GlobalSettings::GlobalSettings(QObject *parent, const QString &configRoot) : QObject(parent)
{
    filepath_ = QDir(configRoot).filePath("ScreenCapturer/capturer.ini");
    legacyFilepath_ = QDir(configRoot).filePath("capturer.json");
    load();
}

GlobalSettings::~GlobalSettings()
{
    theme_monitor.reset();
}

void GlobalSettings::load()
{
    const bool migrate = !QFile::exists(filepath_) && importLegacyConfig();
    QSettings settings(filepath_, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    autorun = settings.value("autorun", autorun).toBool();
    language = settings.value("language", language).toString();
    theme = settings.value("theme", theme).toString();
    if (theme != "auto" && theme != "dark" && theme != "light") theme = "auto";

    const auto readKey = [&](const QString &name, QKeySequence &key) {
        key = QKeySequence::fromString(settings.value("hotkeys/" + name,
                  key.toString(QKeySequence::PortableText)).toString(), QKeySequence::PortableText);
    };
    readKey("screenshot", screenshot);
    readKey("preview", preview);
    readKey("restore-closed-pin", restore_closed_pin);
    readKey("toggle-previews", toggle_previews);
    readKey("quick-look", quick_look);
    readKey("transparent-input", transparent_input);
    readKey("translate-txt", translate_txt);
    readKey("disable-transparent-input", disable_transparent_input);
    style.border_width = settings.value("snip/style/border-width", style.border_width).toInt();
    if (style.border_width < 1 || style.border_width > 6) style.border_width = 2;
    const QColor border(settings.value("snip/style/border-color", style.border_color.name(QColor::HexArgb)).toString());
    if (border.isValid()) style.border_color = border;
    const QColor mask(settings.value("snip/style/mask-color", style.mask_color.name(QColor::HexArgb)).toString());
    if (mask.isValid()) style.mask_color = mask;
    const int pen = settings.value("snip/style/border-style", int(style.border_style)).toInt();
    style.border_style = (pen >= Qt::SolidLine && pen <= Qt::DashDotDotLine)
                           ? static_cast<Qt::PenStyle>(pen) : Qt::SolidLine;
    path = settings.value("snip/save-path", path).toString();
    if (path.isEmpty()) path = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    subfix = settings.value("snip/img-subfix", subfix).toString();
    if (subfix != "PNG(*.png)" && subfix != "JPEG(*.jpg *.jpeg)" && subfix != "BMP(*.bmp)") subfix = "PNG(*.png)";
    imageSuffix = image_save::suffixForFilter(subfix, settings.value("snip/img-suffix", "").toString().toLower());
    moveopaque = settings.value("snip/moveopaque", moveopaque).toFloat();
    if (!std::isfinite(moveopaque) || moveopaque < 0.01f || moveopaque > 1.0f) moveopaque = 0.5f;
    const QStringList mouseActions{"thumbnail", "reset", "fit", "close", "none"};
    const auto readMouseAction = [&](const QString &key, const QString &fallback) {
        const QString action = settings.value("pins/" + key, fallback).toString();
        return mouseActions.contains(action) ? action : fallback;
    };
    pinDoubleClick = readMouseAction("double-click", "thumbnail");
    pinShiftDoubleClick = readMouseAction("shift-double-click", "close");
    pinMiddleClick = readMouseAction("middle-click", "reset");
    pinThumbnailSize = std::clamp(settings.value("pins/thumbnail-size", 60).toInt(), 32, 240);
    closedPinsLimit = std::clamp(settings.value("pins/closed-history-limit", 10).toInt(), 0, 50);
    if (migrate) save();
}

bool GlobalSettings::save()
{
    if (!QDir().mkpath(QFileInfo(filepath_).absolutePath())) {
        qWarning() << "Cannot create settings directory:" << filepath_;
        return false;
    }
    QSettings settings(filepath_, QSettings::IniFormat);
    settings.setFallbacksEnabled(false);
    settings.setValue("autorun", autorun);
    settings.setValue("language", language);
    settings.setValue("theme", theme);
    const auto writeKey = [&](const QString &name, const QKeySequence &key) {
        settings.setValue("hotkeys/" + name, key.toString(QKeySequence::PortableText));
    };
    writeKey("screenshot", screenshot);
    writeKey("preview", preview);
    writeKey("restore-closed-pin", restore_closed_pin);
    writeKey("toggle-previews", toggle_previews);
    writeKey("quick-look", quick_look);
    writeKey("transparent-input", transparent_input);
    writeKey("translate-txt", translate_txt);
    writeKey("disable-transparent-input", disable_transparent_input);
    settings.setValue("snip/style/border-width", style.border_width);
    settings.setValue("snip/style/border-color", style.border_color.name(QColor::HexArgb));
    settings.setValue("snip/style/border-style", int(style.border_style));
    settings.setValue("snip/style/mask-color", style.mask_color.name(QColor::HexArgb));
    settings.setValue("snip/save-path", path);
    settings.setValue("snip/img-subfix", subfix);
    settings.setValue("snip/img-suffix", imageSuffix);
    settings.setValue("snip/moveopaque", moveopaque);
    settings.setValue("pins/double-click", pinDoubleClick);
    settings.setValue("pins/shift-double-click", pinShiftDoubleClick);
    settings.setValue("pins/middle-click", pinMiddleClick);
    settings.setValue("pins/thumbnail-size", pinThumbnailSize);
    settings.setValue("pins/closed-history-limit", closedPinsLimit);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning() << "Cannot save settings:" << filepath_ << settings.status();
        return false;
    }
    return true;
}

bool GlobalSettings::importLegacyConfig()
{
    QFile file(legacyFilepath_);
    if (!file.exists()) return false;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot read legacy settings:" << file.fileName();
        return false;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid legacy settings:" << file.fileName() << error.errorString();
        return false;
    }
    const auto object = document.object();
    const auto readString = [](const QJsonObject &o, const QString &key, QString &value) {
        if (o.value(key).isString()) value = o.value(key).toString();
    };
    if (object.value("autorun").isBool()) autorun = object.value("autorun").toBool();
    readString(object, "language", language);
    readString(object, "theme", theme);
    const auto hotkeys = object.value("hotkeys").toObject();
    const auto readKey = [&](const QString &name, QKeySequence &key) {
        if (hotkeys.value(name).isString()) key = QKeySequence(hotkeys.value(name).toString());
    };
    readKey("screenshot", screenshot);
    readKey("preview", preview);
    readKey("toggle-previews", toggle_previews);
    readKey("quick-look", quick_look);
    readKey("transparent-input", transparent_input);
    readKey("translate-txt", translate_txt);
    readKey("disable-transparent-input", disable_transparent_input);
    const auto snip = object.value("snip").toObject();
    const auto appearance = snip.value("style").toObject();
    if (appearance.value("border-width").isDouble()) style.border_width = appearance.value("border-width").toInt();
    if (appearance.value("border-style").isDouble()) style.border_style = static_cast<Qt::PenStyle>(appearance.value("border-style").toInt());
    const QColor border(appearance.value("border-color").toString());
    if (border.isValid()) style.border_color = border;
    const QColor mask(appearance.value("mask-color").toString());
    if (mask.isValid()) style.mask_color = mask;
    readString(snip, "save-path", path);
    readString(snip, "img-subfix", subfix);
    if (snip.value("moveopaque").isDouble()) moveopaque = static_cast<float>(snip.value("moveopaque").toDouble());
    // Keep the JSON file as a backup. Existing INI files always take precedence.
    return true;
}

QString GlobalSettings::definiteTheme() const
{
    if (theme == "auto") return QString::fromStdString(probe::to_string(probe::system::theme()));
    return theme == "dark" ? "dark" : "light";
}

void GlobalSettings::setTheme(const QString &value)
{
    if (theme == value) return;
    theme = value;
    monitorSystemTheme(theme == "auto");
    emit onThemeChanged(definiteTheme());
}

void GlobalSettings::notifySystemThemeChanged()
{
    // Listener callbacks run on a worker thread. Keep UI updates on the Qt thread,
    // and preserve the user's "auto" preference when the system theme changes.
    QMetaObject::invokeMethod(this, [this] {
        if (theme == "auto") emit onThemeChanged(definiteTheme());
    }, Qt::QueuedConnection);
}

void GlobalSettings::monitorSystemTheme(bool m)
{
#ifdef _WIN32
    if (m && theme_monitor == nullptr) {
        theme_monitor = std::make_shared<probe::util::registry::RegistryListener>();

        theme_monitor->listen(
            std::pair<HKEY, std::string>{
                HKEY_CURRENT_USER, R"(Software\Microsoft\Windows\CurrentVersion\Themes\Personalize)" },
            [this](auto) { notifySystemThemeChanged(); });
    }

    if (!m) {
        theme_monitor = nullptr;
    }
#elif __linux__
    if (m && theme_monitor == nullptr) {
        if (probe::system::desktop_environment() == probe::system::desktop_environment_t::GNOME ||
            probe::system::desktop_environment() == probe::system::desktop_environment_t::Unity) {

            theme_monitor = std::make_shared<probe::util::PipeListener>();

            auto color_scheme = probe::util::exec_sync(
                { "gsettings", "get", "org.gnome.desktop.interface", "color-scheme" });
            if (!color_scheme.empty() && color_scheme[0] != "\t") { // TODO
                theme_monitor->listen(std::vector{ "gsettings", "monitor",
                                                   "org.gnome.desktop.interface", "color-scheme" },
                                      [this](auto) {
                                          notifySystemThemeChanged();
                                      });

                return;
            }

            auto gtk_theme = probe::util::exec_sync(
                { "gsettings", "get", "org.gnome.desktop.interface", "gtk-theme" });
            if (!gtk_theme.empty() && gtk_theme[0] != "\t") {
                theme_monitor->listen(
                    std::vector{ "gsettings", "monitor", "org.gnome.desktop.interface", "gtk-theme" },
                    [this](auto) {

                        notifySystemThemeChanged();
                    });

                return;
            }
        }
    }

    if (!m) {
        theme_monitor = nullptr;
    }
#endif
}

#ifdef _WIN32
namespace {
    bool createShortcut(const QString &srcFilePath, const QString &dstFilePath)
    {
        CoInitialize(NULL);

        IShellLink* pShellLink = nullptr;
        HRESULT hres = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                        IID_IShellLink, (void**)&pShellLink);

        if (SUCCEEDED(hres)) {
            pShellLink->SetPath((LPCWSTR)srcFilePath.utf16());
            pShellLink->SetWorkingDirectory((LPCWSTR)QFileInfo(srcFilePath).absolutePath().utf16());

            IPersistFile* pPersistFile;
            hres = pShellLink->QueryInterface(IID_IPersistFile, (void**)&pPersistFile);

            if (SUCCEEDED(hres)) {
                hres = pPersistFile->Save((LPCWSTR)dstFilePath.utf16(), TRUE);
                pPersistFile->Release();
                pShellLink->Release();
                CoUninitialize();
                return SUCCEEDED(hres);
            }
            pShellLink->Release();
        }
        CoUninitialize();
        return false;
    }

}
#endif

bool GlobalSettings::setAutorun(bool state)
{
#ifdef _WIN32
    QString appPath = QCoreApplication::applicationFilePath();
    QString startupPath = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation)
                          + "\\Startup";

    QString lnkPath = startupPath + "\\" + QCoreApplication::applicationName() + ".lnk";

    if (state) {
        if (createShortcut(appPath, lnkPath)) {
            qDebug() << "自启动快捷方式已创建:" << lnkPath;
        } else {
            qDebug() << "创建快捷方式失败!";
        }
    } else {
        if(QFile::exists(lnkPath)) {
            QFile::remove(lnkPath);
            qDebug() << "已移除自启动快捷方式";
        }
    }

#elif __linux__
    const std::string desktop_file = "/usr/share/applications/capturer.desktop";
    const std::string autorun_dir  = std::string{ ::getenv("HOME") } + "/.config/autostart";
    const std::string autorun_file = autorun_dir + "/capturer.desktop";

    if (!std::filesystem::exists(desktop_file)) {
        logw("failed to set `autorun` since the '{}' does not exists.", desktop_file);
        state = false;
    }

    if (state) {
        if (std::filesystem::exists(desktop_file) && !std::filesystem::exists(autorun_file)) {
            if (!std::filesystem::exists(autorun_dir)) {
                std::filesystem::create_directories(autorun_dir);
            }
            std::filesystem::create_symlink(desktop_file, autorun_file);
        }
    }
    else {
        std::filesystem::remove(autorun_file);
    }
#elif __APPLE__

    bool success = setAutoStart(state);
    if(success) {
        if(state) qDebug() << "设置自启动成功";
        else qDebug() << " 取消自启动成功";
    }

#endif

    autorun = state;

    return state;
}

ImageSaveTarget GlobalSettings::selectImageSaveTarget(QWidget *parent, const QString &caption)
{
    const QString suffix = image_save::suffixForFilter(subfix, imageSuffix);
    const QString defaultName = "S" + QDateTime::currentDateTime().toString("yyyy-MM-dd_hhmmss_zzz") + '.' + suffix;
    QFileDialog dialog(parent, caption, QDir(path).filePath(defaultName), image_save::filters());
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.selectNameFilter(subfix);
    dialog.setDefaultSuffix(suffix);
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&](const QString &filter) {
        dialog.setDefaultSuffix(image_save::suffixForFilter(filter, imageSuffix));
        const auto files = dialog.selectedFiles();
        if (!files.isEmpty()) {
            const auto selected = image_save::target(files.front(), filter, imageSuffix);
            dialog.selectFile(selected.filename);
        }
    });

    while (dialog.exec() == QDialog::Accepted) {
        const auto files = dialog.selectedFiles();
        if (files.isEmpty()) return {};
        const auto selected = image_save::target(files.front(), dialog.selectedNameFilter(), imageSuffix);
        // Some native dialogs keep a manually typed extension when the type changes.
        // If normalization changes the path, confirm replacement of the final file,
        // since the native dialog only confirmed the original path.
        if (QDir::cleanPath(selected.filename) != QDir::cleanPath(files.front()) && QFileInfo::exists(selected.filename)) {
            const auto answer = QMessageBox::question(parent, tr("Save Image"),
                tr("%1 already exists. Do you want to replace it?").arg(QFileInfo(selected.filename).fileName()),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) {
                dialog.selectFile(selected.filename);
                continue;
            }
        }
        return selected;
    }
    return {};
}

bool GlobalSettings::saveImage(const QImage &image, const ImageSaveTarget &target)
{
    if (target.isEmpty() || image.isNull()) return false;
    QSaveFile file(target.filename);
    if (!file.open(QIODevice::WriteOnly) || !image.save(&file, target.format.constData()) || !file.commit()) {
        qWarning() << "Cannot save image:" << target.filename << file.errorString();
        return false;
    }
    // Remember preferences only after the image has been successfully committed.
    path = QFileInfo(target.filename).absolutePath();
    subfix = target.filter;
    imageSuffix = target.suffix;
    save();
    return true;
}

void GlobalSettings::setClosedPinsLimit(int limit)
{
    limit = std::clamp(limit, 0, 50);
    if (closedPinsLimit == limit) return;
    closedPinsLimit = limit;
    emit closedPinsLimitChanged(limit);
    save();
}
