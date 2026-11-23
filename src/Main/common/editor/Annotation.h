#pragma once
#include <QColor>
#include <QFont>
#include <QImage>
#include <QTransform>

namespace annotation {
// Coordinates/font pixels are stored at source-image resolution. The native
// screenshot tools operate in logical scene coordinates; GraphicsTool bridges it.
struct Annotation {
    QString id;
    QString type;
    int toolVersion{1}; // 0: existing pin data, 1: unchanged screenshot tools
    QList<QPointF> points;
    QColor color{Qt::red};
    qreal width{3};
    qreal mosaicSize{9};
    bool filled{false};
    QFont font;
    QString text;
    QString richText;
    QImage image;
    int counter{1};
    qreal angle{0};
    QTransform transform;
    QRectF bounds() const;
    bool operator==(const Annotation &) const = default;
};
}
