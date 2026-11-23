#ifndef GLOBALSETTINGS_H
#define GLOBALSETTINGS_H

#include "selector.h"
#include "ImageSaveFormat.h"
#include <QObject>
#include <QKeySequence>
#include <memory>

class GlobalSettings final : public QObject
{
    Q_OBJECT
public:
    static GlobalSettings *instance();
    ~GlobalSettings() override;
    void load();
    bool save();
    QString filePath() const { return filepath_; }

    bool autorun{true};
    QString language{"zh_CN"};
    QString theme{"auto"};
    QKeySequence screenshot{"F1"};
    QKeySequence preview{"F3"};
    QKeySequence restore_closed_pin{"Ctrl+Shift+F3"};
    QKeySequence toggle_previews{"Shift+F3"};
    QKeySequence quick_look{"F2"};
    QKeySequence transparent_input{"Ctrl+T"};
    QKeySequence disable_transparent_input{"Ctrl+Shift+T"};
    QKeySequence translate_txt{"Ctrl+D"};
    SelectorStyle style{2, "#409EFF", Qt::SolidLine, "#88000000"};
    QString path;
    QString subfix{"PNG(*.png)"};
    QString imageSuffix{"png"};
    float moveopaque{0.5f};
    QString pinDoubleClick{"thumbnail"};
    QString pinShiftDoubleClick{"close"};
    QString pinMiddleClick{"reset"};
    int pinThumbnailSize{60};
    int closedPinsLimit{10};
    void setClosedPinsLimit(int limit);

    QString definiteTheme() const;
    bool setAutorun(bool state);
    void setTheme(const QString &theme);
    void monitorSystemTheme(bool enabled);
    ImageSaveTarget selectImageSaveTarget(QWidget *parent = nullptr, const QString &caption = QString());
    bool saveImage(const QImage &image, const ImageSaveTarget &target);
signals:
    void closedPinsLimitChanged(int limit);
    void onThemeChanged(const QString &theme);
private:
    friend class GlobalSettingsTest;
    explicit GlobalSettings(QObject *parent, const QString &configRoot);
    void notifySystemThemeChanged();
    bool importLegacyConfig();
    QString filepath_;
    QString legacyFilepath_;
    std::shared_ptr<probe::Listener> theme_monitor;
};

#define g_Settings GlobalSettings::instance()
#endif // GLOBALSETTINGS_H
