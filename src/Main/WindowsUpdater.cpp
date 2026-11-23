#include <QtGlobal>

#ifdef Q_OS_WIN
#include "UpdateChecker.h"
#include "UpdatePrompt.h"
#include "version.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QVersionNumber>
#include <memory>
#include <utility>

namespace {
constexpr auto repository = "JackZ2024/ScreenCapturer";
constexpr auto helperName = "ScreenCapturerUpdater.exe";
constexpr auto manifestName = "ScreenCapturer-files.txt";
constexpr qint64 maxReleaseBytes = 1024 * 1024;
constexpr qint64 maxPackageBytes = 1024LL * 1024 * 1024;

QNetworkRequest requestFor(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "ScreenCapturer/" CAPTURER_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(20000);
    return request;
}

QString packageName()
{
    const auto cpu = QSysInfo::buildCpuArchitecture();
    if (cpu == "x86_64") return QStringLiteral("ScreenCapturer-windows-x64.zip");
    if (cpu == "arm64") return QStringLiteral("ScreenCapturer-windows-arm64.zip");
    if (cpu == "i386") return QStringLiteral("ScreenCapturer-windows-x86.zip");
    return {};
}

struct Release {
    QString version;
    QString tag;
    QString notes;
    QVersionNumber number;
    QUrl package;
    QByteArray sha256;
    qint64 size = 0;
};

Release parseRelease(const QByteArray &json, const QString &assetName)
{
    const auto object = QJsonDocument::fromJson(json).object();
    Release release;
    release.tag = object.value("tag_name").toString();
    const QRegularExpression tagPattern(QStringLiteral("^v?([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)$"));
    const auto match = tagPattern.match(release.tag);
    if (!match.hasMatch() || object.value("draft").toBool() || object.value("prerelease").toBool()) {
        return release;
    }

    qsizetype suffix = 0;
    release.version = match.captured(1);
    const auto number = QVersionNumber::fromString(release.version, &suffix);
    if (suffix != release.version.size()) return release;

    release.number = number.normalized();
    release.notes = object.value("body").toString();
    const auto assets = object.value("assets").toArray();
    for (const auto &value : assets) {
        const auto asset = value.toObject();
        if (assetName.isEmpty() || asset.value("name").toString() != assetName ||
            asset.value("state").toString() != "uploaded") continue;

        const QRegularExpression digestPattern(QStringLiteral("^sha256:([0-9a-fA-F]{64})$"));
        const auto digest = digestPattern.match(asset.value("digest").toString());
        const auto size = asset.value("size").toInteger();
        if (!digest.hasMatch() || size <= 0 || size > maxPackageBytes) continue;

        release.package = QUrl(QStringLiteral("https://github.com/%1/releases/download/%2/%3")
                                     .arg(QString::fromLatin1(repository), release.tag, assetName));
        release.sha256 = QByteArray::fromHex(digest.captured(1).toLatin1());
        release.size = size;
        break;
    }
    return release;
}

class WindowsUpdater final : public QObject
{
public:
    explicit WindowsUpdater(QObject *parent) : QObject(parent)
    {
        const auto stages = installDir_.entryInfoList({QStringLiteral(".screencapturer-update-*")},
                                                     QDir::Dirs | QDir::Hidden | QDir::NoSymLinks);
        for (const auto &entry : stages) {
            if (entry.isJunction()) continue;
            QDir stage(entry.absoluteFilePath());
            if (!stage.exists("finished") || stage.exists("backup") || stage.exists("payload")) continue;
            if (stage.exists(helperName) && !stage.remove(helperName)) continue;
            if (stage.remove("finished")) installDir_.rmdir(entry.fileName());
        }
    }

    void check(bool manual, QWidget *parent)
    {
        if (manual) {
            manual_ = true;
            parent_ = parent;
        }
        if (auto *window = prompt_ ? prompt_.data() : progress_.data()) {
            window->show();
            window->raise();
            window->activateWindow();
            return;
        }
        if (reply_) return;

        auto request = requestFor(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                           .arg(QString::fromLatin1(repository))));
        request.setRawHeader("Accept", "application/vnd.github+json");
        auto *reply = network_.get(request);
        reply_ = reply;
        reply->setReadBufferSize(64 * 1024);
        connect(reply, &QIODevice::readyRead, this, [this, reply] {
            releaseData_ += reply->read(maxReleaseBytes + 1 - releaseData_.size());
            if (releaseData_.size() > maxReleaseBytes) reply->abort();
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply] { checkFinished(reply); });
    }

private:
    void checkFinished(QNetworkReply *reply)
    {
        reply_ = nullptr;
        reply->deleteLater();
        const bool manual = std::exchange(manual_, false);
        const auto data = std::exchange(releaseData_, {});
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            if (manual) showMessage(UpdatePrompt::tr("No release is available yet."));
            return;
        }
        if (status != 200 || reply->error() != QNetworkReply::NoError) {
            if (manual) showMessage(UpdatePrompt::tr("Could not check for updates. Please try again."));
            return;
        }

        const auto release = parseRelease(data, packageName());
        if (release.number.isNull()) {
            if (manual) showMessage(UpdatePrompt::tr("The release information is invalid."));
            return;
        }
        if (release.number <= QVersionNumber::fromString(QStringLiteral(CAPTURER_VERSION)).normalized()) {
            if (manual) showMessage(UpdatePrompt::tr("You are using the latest version."));
            return;
        }
        if (release.package.isEmpty()) {
            if (manual) showMessage(UpdatePrompt::tr("Version %1 has no verifiable ZIP package for this computer.").arg(release.version));
            return;
        }

        const auto versionKey = release.number.toString();
        if (!manual && UpdatePrompt::wasSkipped(versionKey)) return;

        auto *dialog = UpdatePrompt::create(release.version, release.notes);
        prompt_ = dialog;
        connect(dialog, &QDialog::finished, this, [this, release, versionKey](int result) {
            prompt_ = nullptr;
            manual_ = false;
            if (result == QDialog::Accepted) download(release);
            else UpdatePrompt::rememberSkipped(versionKey);
        });
        dialog->show();
        dialog->raise();
        dialog->activateWindow();
    }

    void download(const Release &release)
    {
        if (!installDir_.exists(helperName) || !installDir_.exists(manifestName)) {
            showMessage(UpdatePrompt::tr("Extract an official portable ZIP before using automatic updates."));
            return;
        }
        stage_ = std::make_unique<QTemporaryDir>(installDir_.filePath(".screencapturer-update-XXXXXX"));
        if (!stage_->isValid()) {
            stage_.reset();
            showMessage(UpdatePrompt::tr("Could not create a folder for the update."));
            return;
        }
        archive_.setFileName(stage_->filePath("update.zip"));
        if (!archive_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
            stage_.reset();
            showMessage(UpdatePrompt::tr("Could not create the ZIP file."));
            return;
        }

        hash_.reset();
        auto *progress = new QProgressDialog(UpdatePrompt::tr("Downloading update…"), UpdatePrompt::tr("Cancel"),
                                             0, 100, nullptr, Qt::WindowStaysOnTopHint);
        progress->setWindowTitle(UpdatePrompt::tr("Software Update"));
        progress->setAutoClose(false);
        progress->setAutoReset(false);
        progress->setValue(0);
        progress_ = progress;

        auto *reply = network_.get(requestFor(release.package));
        reply_ = reply;
        reply->setReadBufferSize(256 * 1024);
        connect(progress, &QProgressDialog::canceled, this, [this, reply] {
            cancelled_ = true;
            reply->abort();
        });
        connect(reply, &QIODevice::readyRead, this, [this, reply, release] {
            const auto chunk = reply->readAll();
            if (archive_.size() + chunk.size() > release.size || archive_.write(chunk) != chunk.size()) {
                reply->abort();
                return;
            }
            hash_.addData(chunk);
            progress_->setValue(static_cast<int>(archive_.size() * 100 / release.size));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, release] { downloadFinished(reply, release); });
        progress->show();
    }

    void downloadFinished(QNetworkReply *reply, const Release &release)
    {
        reply_ = nullptr;
        reply->deleteLater();
        disconnect(progress_, nullptr, this, nullptr);

        const bool verified = reply->error() == QNetworkReply::NoError &&
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 &&
            archive_.size() == release.size && hash_.result() == release.sha256 && archive_.flush();
        archive_.close();
        if (std::exchange(cancelled_, false)) {
            finishPreparation();
        } else if (!verified) {
            finishPreparation(UpdatePrompt::tr("The ZIP download or verification failed. Please try again."));
        } else {
            prepare();
        }
    }

    void prepare()
    {
        const auto helper = stage_->filePath(helperName);
        if (!QFile::copy(installDir_.filePath(helperName), helper)) {
            finishPreparation(UpdatePrompt::tr("Could not start the update assistant."));
            return;
        }

        progress_->setLabelText(UpdatePrompt::tr("Preparing update…"));
        progress_->setCancelButton(nullptr);
        progress_->setRange(0, 0);
        auto *process = new QProcess(this);
        process->setWorkingDirectory(stage_->path());
        const auto root = QDir::toNativeSeparators(installDir_.absolutePath());
        const auto stage = QDir::toNativeSeparators(stage_->path());
        const auto error = UpdatePrompt::tr("Could not prepare the ZIP update.");
        connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart) return;
            process->deleteLater();
            finishPreparation(UpdatePrompt::tr("Could not start the update assistant."));
        });
        connect(process, &QProcess::finished, this, [this, process, helper, root, stage, error](int code, QProcess::ExitStatus status) {
            const auto detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            process->deleteLater();
            if (code != 0 || status != QProcess::NormalExit) {
                finishPreparation(detail.isEmpty() ? error : error + "\n\n" + detail);
                return;
            }
            const QStringList arguments{QStringLiteral("--apply"), root, stage,
                                        QString::number(QCoreApplication::applicationPid())};
            if (!QProcess::startDetached(helper, arguments, stage_->path())) {
                finishPreparation(UpdatePrompt::tr("Could not start the update assistant."));
                return;
            }
            stage_->setAutoRemove(false);
            finishPreparation();
            QCoreApplication::quit();
        });
        process->start(helper, {QStringLiteral("--prepare"), root, stage});
    }

    void finishPreparation(const QString &error = {})
    {
        progress_->hide();
        progress_->deleteLater();
        progress_ = nullptr;
        stage_.reset();
        manual_ = false;
        if (!error.isEmpty()) showMessage(error);
    }

    void showMessage(const QString &message)
    {
        QMessageBox::information(parent_, UpdatePrompt::tr("Software Update"), message);
    }

    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QPointer<QWidget> parent_;
    QPointer<QDialog> prompt_;
    QPointer<QProgressDialog> progress_;
    bool manual_ = false;
    bool cancelled_ = false;
    QByteArray releaseData_;
    QDir installDir_{QFileInfo(QCoreApplication::applicationFilePath()).canonicalPath()};
    std::unique_ptr<QTemporaryDir> stage_;
    QFile archive_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
};

WindowsUpdater *updater()
{
    static auto *instance = new WindowsUpdater(qApp);
    return instance;
}
}

void update_checker::checkOnStartup()
{
    updater()->check(false, nullptr);
}

void update_checker::checkManually(QWidget *parent)
{
    updater()->check(true, parent);
}
#endif
