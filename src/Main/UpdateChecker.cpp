#include "UpdateChecker.h"
#include "UpdatePrompt.h"
#include "GlobalSettings.h"
#include "version.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {
QString skippedVersionKey(const QString &version)
{
    return QStringLiteral("updates/skipped-versions/") + version;
}
}

QDialog *UpdatePrompt::create(const QString &version, const QString &notes)
{
    auto *dialog = new QDialog(nullptr, Qt::WindowTitleHint | Qt::WindowCloseButtonHint |
                                       Qt::WindowStaysOnTopHint);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("Software Update"));
    dialog->setMinimumWidth(380);

    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(tr("Version %1 is available. You are using %2.")
                                 .arg(version, QStringLiteral(CAPTURER_VERSION)), dialog);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    layout->addWidget(label);

    if (!notes.trimmed().isEmpty()) {
        auto *releaseNotes = new QPlainTextEdit(dialog);
        releaseNotes->setPlainText(notes);
        releaseNotes->setReadOnly(true);
        layout->addWidget(releaseNotes);
    }

    auto *buttons = new QDialogButtonBox(dialog);
    buttons->addButton(tr("Update"), QDialogButtonBox::AcceptRole)->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    layout->addWidget(buttons);
    return dialog;
}

bool UpdatePrompt::wasSkipped(const QString &version)
{
    const QSettings settings(g_Settings->filePath(), QSettings::IniFormat);
    return settings.value(skippedVersionKey(version), false).toBool();
}

void UpdatePrompt::rememberSkipped(const QString &version)
{
    QSettings settings(g_Settings->filePath(), QSettings::IniFormat);
    settings.setValue(skippedVersionKey(version), true);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning("Could not save the skipped update version.");
    }
}

#if !defined(Q_OS_WIN) && !defined(SCREENCAPTURER_WITH_SPARKLE)
void update_checker::checkOnStartup() {}

void update_checker::checkManually(QWidget *parent)
{
    QMessageBox::information(parent, UpdatePrompt::tr("Software Update"),
                             UpdatePrompt::tr("Updates are unavailable in this build."));
}
#endif
