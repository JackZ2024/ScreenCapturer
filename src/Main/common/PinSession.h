#ifndef CAPTURER_PIN_SESSION_H
#define CAPTURER_PIN_SESSION_H

#include <QImage>
#include <QJsonArray>
#include <QList>
#include <QRect>
#include <QString>

struct PinnedImageState
{
    QImage image;
    QRect geometry;
    QRect thumbnailSource; // Image pixels, independent of window position and screen scaling.
    qreal scale{1.0};
    qreal opacity{1.0};
    bool thumbnail{false};
    QString background;
    QImage originalImage;
    bool transparentInput{false};
    QImage annotationBase;
    QJsonArray annotations;
};

namespace pin_session
{
    QString directory();
    bool save(const QString &directory, const QList<PinnedImageState> &images);
    QList<PinnedImageState> load(const QString &directory);
    QList<PinnedImageState> loadLegacy(const QString &directory);
    QRect visibleGeometry(const QRect &geometry, const QList<QRect> &screens);
}
#endif
