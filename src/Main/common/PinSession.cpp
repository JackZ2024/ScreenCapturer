#include "PinSession.h"
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
QJsonArray rectJson(const QRect &rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}
QRect readRect(const QJsonValue &value)
{
    const auto a = value.toArray();
    if (a.size() != 4) return {};
    return {a[0].toInt(), a[1].toInt(), a[2].toInt(), a[3].toInt()};
}
qreal validReal(const QJsonValue &value, qreal fallback, qreal min, qreal max)
{
    const double number = value.toDouble(fallback);
    return std::isfinite(number) && number >= min && number <= max ? number : fallback;
}
QJsonObject readObject(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning() << "Invalid pinned image record:" << path;
        return {};
    }
    return document.object();
}
}

QString pin_session::directory()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/pins";
}

bool pin_session::save(const QString &directory, const QList<PinnedImageState> &images)
{
    QDir dir(directory);
    if (!QDir().mkpath(dir.absolutePath())) {
        qWarning() << "Could not create pinned image directory:" << dir.absolutePath();
        return false;
    }
    QStringList created;
    QJsonArray entries;
    const auto rollback = [&] {
        for (const auto &name : created) QFile::remove(dir.filePath(name));
        qWarning() << "Could not save pinned image session:" << directory;
        return false;
    };
    for (const auto &state : images) {
        if (state.image.isNull() || !state.geometry.isValid()) return rollback();
        const QString name = "pin-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
        QSaveFile file(dir.filePath(name));
        if (!file.open(QIODevice::WriteOnly) || !state.image.save(&file, "PNG") || !file.commit())
            return rollback();
        created.push_back(name);
        QString originalName = name;
        if (!state.originalImage.isNull() && state.originalImage != state.image) {
            originalName = "pin-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
            QSaveFile original(dir.filePath(originalName));
            if (!original.open(QIODevice::WriteOnly) || !state.originalImage.save(&original, "PNG") || !original.commit())
                return rollback();
            created.push_back(originalName);
        }
        QString baseName;
        if (!state.annotationBase.isNull() && !state.annotations.isEmpty()) {
            baseName = "pin-" + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".png";
            QSaveFile base(dir.filePath(baseName));
            if (!base.open(QIODevice::WriteOnly) || !state.annotationBase.save(&base, "PNG") || !base.commit())
                return rollback();
            created.push_back(baseName);
        }
        entries.append(QJsonObject{
            {"annotationBase", baseName}, {"annotations", state.annotations},
            {"annotationDpr", state.annotationBase.isNull() ? 1.0 : state.annotationBase.devicePixelRatio()},
            {"image", name}, {"original", originalName},
            {"originalDpr", state.originalImage.isNull() ? state.image.devicePixelRatio() : state.originalImage.devicePixelRatio()},
            {"transparentInput", state.transparentInput}, {"geometry", rectJson(state.geometry)},
            {"thumbnailSource", rectJson(state.thumbnailSource)},
            {"scale", state.scale}, {"opacity", state.opacity},
            {"dpr", state.image.devicePixelRatio()}, {"thumbnail", state.thumbnail},
            {"background", state.background}
        });
    }
    // Only switch the manifest after every image has been committed successfully.
    QSaveFile manifest(dir.filePath("session.json"));
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"images", entries}}).toJson();
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() || !manifest.commit())
        return rollback();
    // Delete only files owned by this store, and only after committing the new session.
    for (const auto &name : dir.entryList({"pin-*.png"}, QDir::Files)) {
        if (!created.contains(name)) QFile::remove(dir.filePath(name));
    }
    return true;
}

QList<PinnedImageState> pin_session::load(const QString &directory)
{
    QDir dir(directory);
    const auto object = readObject(dir.filePath("session.json"));
    if (object.value("version").toInt() != 1 || !object.value("images").isArray()) return {};
    QList<PinnedImageState> result;
    for (const auto &value : object.value("images").toArray()) {
        const auto entry = value.toObject();
        const auto name = entry.value("image").toString();
        // Manifest entries may only reference files in the session directory.
        if (name.isEmpty() || name != QFileInfo(name).fileName() || name.contains('\\') || name.contains(':')) continue;
        PinnedImageState state;
        state.image.load(dir.filePath(name));
        state.geometry = readRect(entry.value("geometry"));
        if (state.image.isNull() || !state.geometry.isValid()) {
            qWarning() << "Skipping missing or invalid pinned image:" << name;
            continue;
        }
        state.image.setDevicePixelRatio(validReal(entry.value("dpr"), 1.0, 0.25, 16.0));
        state.scale = validReal(entry.value("scale"), 1.0, 0.001, 1000.0);
        state.opacity = validReal(entry.value("opacity"), 1.0, 0.01, 1.0);
        state.thumbnail = entry.value("thumbnail").toBool();
        state.thumbnailSource = readRect(entry.value("thumbnailSource")).intersected(state.image.rect());
        state.background = entry.value("background").toString();
        const QString original = entry.value("original").toString();
        if (!original.isEmpty() && original == QFileInfo(original).fileName() && !original.contains('\\') && !original.contains(':'))
            state.originalImage.load(dir.filePath(original));
        if (state.originalImage.isNull()) state.originalImage = state.image;
        else state.originalImage.setDevicePixelRatio(validReal(entry.value("originalDpr"), 1.0, 0.25, 16.0));
        state.transparentInput = entry.value("transparentInput").toBool();
        const QString baseName = entry.value("annotationBase").toString();
        if (!baseName.isEmpty() && baseName == QFileInfo(baseName).fileName() && !baseName.contains('\\') && !baseName.contains(':'))
            state.annotationBase.load(dir.filePath(baseName));
        if (!state.annotationBase.isNull()) {
            state.annotationBase.setDevicePixelRatio(validReal(entry.value("annotationDpr"), 1.0, 0.25, 16.0));
            state.annotations = entry.value("annotations").toArray();
        }
        result.push_back(state);
    }
    // Loading never consumes or deletes recovery files.
    return result;
}

QList<PinnedImageState> pin_session::loadLegacy(const QString &directory)
{
    QDir dir(directory);
    QList<PinnedImageState> result;
    for (const auto &name : dir.entryList({"*.json"}, QDir::Files)) {
        const auto object = readObject(dir.filePath(name));
        const auto id = object.value("fileName").toString();
        if (id.isEmpty() || id != QFileInfo(id).fileName()) continue;
        PinnedImageState state;
        state.image.load(dir.filePath(id + ".png"));
        if (state.image.isNull()) state.image.load(object.value("filePath").toString());
        state.geometry = QRect(object.value("geoX").toInt(), object.value("geoY").toInt(),
                               object.value("geoW").toInt(), object.value("geoH").toInt());
        if (state.image.isNull() || !state.geometry.isValid()) continue;
        state.scale = validReal(object.value("scale"), 1.0, 0.001, 1000.0);
        state.opacity = validReal(object.value("opacity"), 1.0, 0.01, 1.0);
        state.thumbnail = object.value("thumbnail").toBool();
        result.push_back(state);
    }
    return result;
}

QRect pin_session::visibleGeometry(const QRect &geometry, const QList<QRect> &screens)
{
    if (screens.isEmpty()) return geometry;
    // Keep the screen containing the window's center, otherwise use the primary screen.
    QRect available = screens.front();
    for (const auto &screen : screens) {
        if (screen.contains(geometry.center())) { available = screen; break; }
    }
    const QSize size = geometry.size().boundedTo(available.size()).expandedTo(QSize(1, 1));
    QRect result(geometry.topLeft(), size);
    result.moveLeft(std::clamp(result.left(), available.left(), available.right() - size.width() + 1));
    result.moveTop(std::clamp(result.top(), available.top(), available.bottom() - size.height() + 1));
    return result;
}
