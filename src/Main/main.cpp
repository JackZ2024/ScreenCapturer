#include "Capturer.h"
#include "GlobalSettings.h"
#include "UpdateChecker.h"
#include "logging.h"
#include "version.h"
#include <QTranslator>
#include <QTimer>


int main(int argc, char *argv[])
{
    qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");

    QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);

    Capturer app(argc, argv);
    GlobalSettings::instance();
    QApplication::setQuitOnLastWindowClosed(false);

    // glogs
    Logger::init(argv[0]);

    logi("ScreenCapturer               {}", CAPTURER_VERSION);
    logi(" -- Qt               : {}", qVersion());

    QTranslator translator;
    const auto translation = ":/i18n/Capturer_" + g_Settings->language + ".qm";
    loge_if(!translator.load(translation), "failed to load {}", translation.toStdString());
    Capturer::installTranslator(&translator);

    app.Init();
    QTimer::singleShot(0, &app, [] { update_checker::checkOnStartup(); });

    return Capturer::exec();
}
