#pragma once
#include "Annotation.h"
#include "../../snipping/canvas/graphicsitems.h"
#include <functional>
#include <memory>

namespace annotation {
canvas::graphics_t graph(const QString &type);
QString type(canvas::graphics_t graph);
bool isStroke(const QString &type);
bool isEffect(const QString &type);

// Owns an actual screenshot Graphics*Item, not a second implementation of it.
// The scene borrows the item. Destroy the Tool before destroying its scene.
class GraphicsTool final {
public:
    GraphicsTool(const Annotation &, const QImage &base, const QImage &texture = {});
    ~GraphicsTool();
    GraphicsTool(const GraphicsTool &) = delete;
    GraphicsTool &operator=(const GraphicsTool &) = delete;
    GraphicsItemWrapper *graphics() const;
    QGraphicsItem *item() const;
    GraphicsTextItem *textItem() const;
    Annotation capture() const;
    void apply(const Annotation &);
    void push(const QPointF &logicalPoint);
    void finish();
    void stopGesture();
    void preparePress(const QPointF &scenePoint);
    void selectBrush(bool enabled);
    void changeStyle(const QPen &, const QFont &, bool filled, qreal mosaicSize);
    void effectPreview(std::function<QImage()>);
    bool invalid() const;
    qreal ratio() const { return ratio_; }
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Annotation data_;
    QImage base_;
    qreal ratio_{1};
};
QRectF bounds(const Annotation &, qreal ratio = 1);
void paint(QPainter &, const Annotation &, qreal ratio = 1);
QImage render(const QImage &base, const QList<Annotation> &items);
}
