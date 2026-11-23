#include "GraphicsTool.h"
#include <QGraphicsScene>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTextDocument>
#include <QScreen>
#include <QGuiApplication>
#include <algorithm>
#include <cmath>
#include <type_traits>

namespace annotation {
namespace {
qreal dpi() { return QGuiApplication::primaryScreen() ? QGuiApplication::primaryScreen()->logicalDotsPerInchY() : 96; }
QFont logicalFont(QFont font, qreal ratio)
{
    const qreal pixels = font.pixelSize() > 0 ? font.pixelSize() : std::max(1.0, font.pointSizeF()) * dpi() / 72;
    font.setPointSizeF(pixels / ratio * 72 / dpi());
    return font;
}
QFont physicalFont(QFont font, qreal ratio)
{
    const qreal pixels = font.pixelSize() > 0 ? font.pixelSize() : std::max(1.0, font.pointSizeF()) * dpi() / 72;
    font.setPointSizeF(std::clamp(pixels * ratio, 4.0, 500.0) * 72 / dpi());
    return font;
}
class Adapter {
public:
    virtual ~Adapter() = default;
    virtual void legacy(bool) = 0;
    virtual bool nativeInvalid() const = 0;
    virtual void initialTransform(const QTransform &, qreal angle) = 0;
    virtual QList<QPointF> vertices() const = 0;
    virtual void vertices(const QList<QPointF> &) = 0;
    virtual void stopGesture() = 0;
    virtual void preparePress(const QPointF &) = 0;
    virtual void selectBrush(bool) = 0;
    virtual void restoreGeometry(const ResizerF &) = 0;
    virtual void preview(std::function<QImage()>, QRectF) = 0;
};

// Only coordinate/persistence/lifetime adaptation lives here. Painting and
// pointer interaction delegate to the unchanged screenshot tool classes.
template<class Native> class StoredTool final : public Native, public Adapter {
public:
    template<class... Args> explicit StoredTool(Args &&...args) : Native(std::forward<Args>(args)...) {}
    void legacy(bool enabled) override { legacy_ = enabled; }
    bool nativeInvalid() const override { return Native::invalid(); }
    bool invalid() const override
    {
        if constexpr (std::is_base_of_v<GraphicsItem, Native> && !std::is_base_of_v<GraphicsPathItem, Native>)
            if (legacy_ && QLineF(this->geometry().point1(), this->geometry().point2()).length() > 0.001) return false;
        return Native::invalid();
    }
    void initialTransform(const QTransform &matrix, qreal angle) override
    {
        Native::rotate(angle);
        baseMatrix_ = this->transform().inverted() * matrix;
        this->setTransform(matrix);
    }
    void rotate(qreal angle) override
    {
        Native::rotate(angle);
        this->setTransform(this->transform() * baseMatrix_);
    }
    QList<QPointF> vertices() const override
    {
        if constexpr (std::is_base_of_v<GraphicsPathItem, Native>) return this->vertexes_;
        else return {};
    }
    void vertices(const QList<QPointF> &points) override
    {
        if constexpr (std::is_base_of_v<GraphicsPathItem, Native>) {
            this->prepareGeometryChange();
            this->vertexes_ = points;
            if (!points.isEmpty() && std::all_of(points.begin(), points.end(), [&](const auto &p) { return p == points.front(); }))
                this->vertexes_.append(points.front() + QPointF(0.001, 0));
            this->tmpvtxes_ = this->vertexes_;
            // Prevent focus changes from cropping the persistent stroke buffer.
            this->fixed_ = true;
            this->redraw();
        }
    }
    void stopGesture() override { this->adjusting_ = canvas::adjusting_t::none; }
    void restoreGeometry(const ResizerF &geometry) override
    {
        if constexpr (std::is_same_v<Native, GraphicsPixmapItem> || std::is_same_v<Native, GraphicsCounterleItem>) {
            this->prepareGeometryChange();
            this->geometry_.coords(geometry.point1(), geometry.point2());
        } else Native::resize(geometry, ResizerLocation::DEFAULT);
    }
    void preparePress(const QPointF &point) override { this->hover_location_ = this->location(this->mapFromScene(point)); }
    void selectBrush(bool enabled) override
    {
        if constexpr (std::is_base_of_v<GraphicsPathItem, Native>) {
            brushSelection_ = enabled;
            this->setFlag(QGraphicsItem::ItemIsMovable, enabled);
            this->setFlag(QGraphicsItem::ItemIsFocusable, enabled);
            this->setFlag(QGraphicsItem::ItemIsSelectable, enabled);
            this->setAcceptHoverEvents(enabled);
            this->setAcceptedMouseButtons(enabled ? Qt::LeftButton : Qt::NoButton);
        }
    }
    ResizerLocation location(const QPointF &point) const override
    {
        const auto nativeLocation = Native::location(point);
        if constexpr (std::is_base_of_v<GraphicsPathItem, Native>)
            if (brushSelection_ && nativeLocation == ResizerLocation::DEFAULT)
                return this->shape().contains(point) ? ResizerLocation::BORDER : ResizerLocation::OUTSIDE;
        return nativeLocation;
    }
    void preview(std::function<QImage()> callback, QRectF canvas) override { preview_ = std::move(callback); canvas_ = canvas; }
    QRectF boundingRect() const override
    {
        if (preview_) return this->sceneTransform().inverted().mapRect(canvas_).united(Native::boundingRect());
        return Native::boundingRect();
    }
    void paint(QPainter *p, const QStyleOptionGraphicsItem *option, QWidget *widget) override
    {
        if (!preview_) { Native::paint(p, option, widget); return; }
        // Effects replace preceding pixels, including alpha. The same native
        // effect tools generate those pixels in render(); later items stay above.
        p->save();
        p->setWorldTransform(this->sceneTransform().inverted() * p->worldTransform());
        p->setCompositionMode(QPainter::CompositionMode_Source);
        p->drawImage(canvas_, preview_());
        p->restore();
    }
private:
    QTransform baseMatrix_;
    bool brushSelection_{false};
    bool legacy_{false};
    std::function<QImage()> preview_;
    QRectF canvas_;
};

void paintNative(QPainter &p, GraphicsTool &tool, qreal ratio)
{
    QStyleOptionGraphicsItem option;
    option.state = QStyle::State_None;
    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.scale(ratio, ratio);
    p.setTransform(tool.item()->sceneTransform(), true);
    tool.item()->paint(&p, &option, nullptr);
    p.restore();
}
}

struct GraphicsTool::Impl {
    std::unique_ptr<GraphicsItemWrapper> native;
    Adapter *adapter{};
    qreal textMargin{};
};
canvas::graphics_t graph(const QString &type)
{
    if (type == "rectangle") return canvas::rectangle;
    if (type == "ellipse") return canvas::ellipse;
    if (type == "arrow") return canvas::arrow;
    if (type == "line") return canvas::line;
    if (type == "pen") return canvas::curve;
    if (type == "text") return canvas::text;
    if (type == "mosaic") return canvas::mosaic;
    if (type == "eraser") return canvas::eraser;
    if (type == "image") return canvas::pixmap;
    if (type == "counter") return canvas::counter;
    return canvas::none;
}
QString type(canvas::graphics_t graph)
{
    switch (graph) {
    case canvas::rectangle: return "rectangle";
    case canvas::ellipse: return "ellipse";
    case canvas::arrow: return "arrow";
    case canvas::line: return "line";
    case canvas::curve: return "pen";
    case canvas::text: return "text";
    case canvas::mosaic: return "mosaic";
    case canvas::eraser: return "eraser";
    case canvas::pixmap: return "image";
    case canvas::counter: return "counter";
    default: return {};
    }
}
bool isStroke(const QString &type) { return type == "pen" || isEffect(type); }
bool isEffect(const QString &type) { return type == "mosaic" || type == "eraser"; }

GraphicsTool::GraphicsTool(const Annotation &a, const QImage &base, const QImage &texture)
    : impl_(std::make_unique<Impl>()), data_(a), base_(base), ratio_(std::max(0.01, base.devicePixelRatio()))
{
    const QPointF first = a.points.isEmpty() ? QPointF() : a.points.front() / ratio_;
    const QPointF last = a.points.isEmpty() ? first : a.points.back() / ratio_;
    QPixmap brush = QPixmap::fromImage(texture.isNull() ? base : texture);
    brush.setDevicePixelRatio(ratio_);
    const QSizeF size = base.size() / ratio_;
    if (a.type == "rectangle") impl_->native = std::make_unique<StoredTool<GraphicsRectItem>>(first, last);
    else if (a.type == "ellipse") impl_->native = std::make_unique<StoredTool<GraphicsEllipseleItem>>(first, last);
    else if (a.type == "arrow") impl_->native = std::make_unique<StoredTool<GraphicsArrowItem>>(first, last);
    else if (a.type == "line") impl_->native = std::make_unique<StoredTool<GraphicsLineItem>>(first, last);
    else if (a.type == "text") impl_->native = std::make_unique<StoredTool<GraphicsTextItem>>(QString(), QPointF());
    else if (a.type == "pen") impl_->native = std::make_unique<StoredTool<GraphicsCurveItem>>(first, size, brush);
    else if (a.type == "mosaic") impl_->native = std::make_unique<StoredTool<GraphicsMosaicItem>>(first, size, brush);
    else if (a.type == "eraser") impl_->native = std::make_unique<StoredTool<GraphicsEraserItem>>(first, size, brush);
    else if (a.type == "image") impl_->native = std::make_unique<StoredTool<GraphicsPixmapItem>>(QPixmap::fromImage(a.image), QPointF());
    else if (a.type == "counter") impl_->native = std::make_unique<StoredTool<GraphicsCounterleItem>>(QPointF(), a.counter);
    else return;
    impl_->adapter = dynamic_cast<Adapter *>(graphics());
    if (const auto *text = textItem()) impl_->textMargin = text->document()->documentMargin();
    item()->setData(0, a.id);
    if (!isStroke(a.type)) item()->setFlag(QGraphicsItem::ItemIsSelectable);
    apply(a);
}
GraphicsTool::~GraphicsTool()
{
    if (item() && item()->scene()) item()->scene()->removeItem(item());
}
GraphicsItemWrapper *GraphicsTool::graphics() const { return impl_->native.get(); }
QGraphicsItem *GraphicsTool::item() const { return dynamic_cast<QGraphicsItem *>(graphics()); }
GraphicsTextItem *GraphicsTool::textItem() const { return dynamic_cast<GraphicsTextItem *>(graphics()); }
void GraphicsTool::apply(const Annotation &a)
{
    if (!graphics()) return;
    data_ = a;
    impl_->adapter->legacy(a.toolVersion == 0);
    graphics()->setDevicePixelRatio(ratio_);
    item()->setPos(QPointF());
    if (auto *text = textItem()) {
        text->document()->setDocumentMargin(a.toolVersion == 0 ? 0 : impl_->textMargin);
        text->setFont(logicalFont(a.font, ratio_));
        if (a.richText.isEmpty()) text->setPlainText(a.text); else text->setHtml(a.richText);
        text->setDefaultTextColor(a.color);
        text->setTextInteractionFlags(Qt::NoTextInteraction);
    } else if (isStroke(a.type)) {
        QList<QPointF> points;
        for (const auto &point : a.points) points.append(point / ratio_);
        impl_->adapter->vertices(points);
    } else if (a.points.size() >= 2) impl_->adapter->restoreGeometry(ResizerF(a.points.front() / ratio_, a.points.back() / ratio_));
    if (auto *image = dynamic_cast<GraphicsPixmapItem *>(graphics())) {
        // Restored geometry belongs to the saved image, including resized/flipped bounds.
        image->setPixmap(QPixmap::fromImage(a.image));
        if (a.points.size() >= 2) impl_->adapter->restoreGeometry(ResizerF(a.points.front() / ratio_, a.points.back() / ratio_));
    }
    if (auto *counter = dynamic_cast<GraphicsCounterleItem *>(graphics())) counter->setCounter(a.counter);
    graphics()->setPen(QPen(a.color, a.width));
    graphics()->fill(a.filled);
    const auto scale = QTransform::fromScale(ratio_, ratio_);
    QTransform matrix = scale * a.transform * scale.inverted();
    if (textItem() && !a.points.isEmpty()) matrix = QTransform::fromTranslate(a.points.front().x() / ratio_, a.points.front().y() / ratio_) * matrix;
    if (a.type == "counter" && a.points.size() == 1) {
        const auto offset = a.points.front() / ratio_ - graphics()->geometry().center();
        matrix = QTransform::fromTranslate(offset.x(), offset.y()) * matrix;
    }
    impl_->adapter->initialTransform(matrix, a.angle);
    item()->update();
}
Annotation GraphicsTool::capture() const
{
    auto a = data_;
    if (!graphics()) return a;
    const auto scale = QTransform::fromScale(ratio_, ratio_);
    a.transform = scale.inverted() * item()->sceneTransform() * scale;
    a.angle = graphics()->angle();
    // Old pins could contain shapes smaller than the screenshot tool's creation
    // threshold. Keep those editable/visible until resized into the normal range.
    if (!textItem() && !impl_->adapter->nativeInvalid()) a.toolVersion = 1;
    a.width = graphics()->pen().widthF();
    if (!isEffect(a.type)) a.color = graphics()->pen().color();
    a.filled = graphics()->filled();
    if (const auto *text = textItem()) {
        a.points = {QPointF()};
        a.text = text->toPlainText();
        a.richText = text->toHtml();
        a.font = physicalFont(text->font(), ratio_);
    } else if (isStroke(a.type)) {
        a.points.clear();
        for (const auto &point : impl_->adapter->vertices()) a.points.append(point * ratio_);
    } else a.points = {graphics()->geometry().point1() * ratio_, graphics()->geometry().point2() * ratio_};
    return a;
}
void GraphicsTool::push(const QPointF &point) { if (graphics()) graphics()->push(point); }
void GraphicsTool::finish() { if (graphics()) graphics()->end(); }
void GraphicsTool::stopGesture() { if (impl_->adapter) impl_->adapter->stopGesture(); }
void GraphicsTool::preparePress(const QPointF &point) { if (impl_->adapter) impl_->adapter->preparePress(point); }
void GraphicsTool::selectBrush(bool enabled) { if (impl_->adapter) impl_->adapter->selectBrush(enabled); }
void GraphicsTool::changeStyle(const QPen &pen, const QFont &font, bool filled, qreal mosaicSize)
{
    data_.mosaicSize = mosaicSize;
    if (!graphics()) return;
    graphics()->setPen(pen);
    graphics()->fill(filled);
    if (textItem()) graphics()->setFont(font);
    item()->update();
}
void GraphicsTool::effectPreview(std::function<QImage()> callback)
{
    if (impl_->adapter) impl_->adapter->preview(std::move(callback), QRectF(QPointF(), base_.size() / ratio_));
}
bool GraphicsTool::invalid() const { return !graphics() || graphics()->invalid(); }

QRectF bounds(const Annotation &a, qreal ratio)
{
    if (a.points.isEmpty()) return {};
    QImage dummy(1, 1, QImage::Format_ARGB32_Premultiplied);
    dummy.fill(Qt::transparent); dummy.setDevicePixelRatio(ratio);
    GraphicsTool tool(a, dummy);
    if (!tool.item()) return {};
    return QTransform::fromScale(ratio, ratio).mapRect(tool.item()->sceneBoundingRect());
}
QRectF Annotation::bounds() const { return annotation::bounds(*this); }
void paint(QPainter &p, const Annotation &a, qreal ratio)
{
    if (isEffect(a.type)) return;
    const QSize size = p.device() ? QSize(p.device()->width(), p.device()->height()) : QSize(1, 1);
    QImage dummy(size, QImage::Format_ARGB32_Premultiplied);
    dummy.fill(Qt::transparent); dummy.setDevicePixelRatio(ratio);
    GraphicsTool tool(a, dummy);
    if (tool.item()) paintNative(p, tool, ratio);
}
QImage render(const QImage &base, const QList<Annotation> &items)
{
    if (base.isNull()) return {};
    const qreal ratio = base.devicePixelRatio();
    QImage image = base.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(1);
    for (const auto &a : items) {
        if (a.points.isEmpty()) continue;
        if (!isEffect(a.type)) {
            GraphicsTool tool(a, base);
            if (!tool.item()) continue;
            QPainter p(&image); paintNative(p, tool, ratio);
            continue;
        }
        QImage texture = a.type == "eraser" ? base : image;
        if (a.type == "mosaic") {
            const int block = std::clamp(qRound(a.mosaicSize), 2, 256);
            texture = texture.scaled(QSize(std::max(1, image.width() / block), std::max(1, image.height() / block)),
                                     Qt::IgnoreAspectRatio, Qt::SmoothTransformation).scaled(image.size(), Qt::IgnoreAspectRatio);
        }
        texture.setDevicePixelRatio(ratio);
        QImage layer(image.size(), QImage::Format_ARGB32_Premultiplied); layer.fill(Qt::transparent);
        GraphicsTool effect(a, base, texture);
        { QPainter p(&layer); paintNative(p, effect, ratio); }
        // Derive coverage from the same native effect class too: future brush
        // shape changes must affect both its texture and its alpha mask.
        QImage white(image.size(), QImage::Format_ARGB32_Premultiplied);
        white.fill(Qt::white); white.setDevicePixelRatio(ratio);
        QImage mask(image.size(), QImage::Format_ARGB32_Premultiplied); mask.fill(Qt::transparent);
        GraphicsTool stroke(a, base, white);
        { QPainter p(&mask); paintNative(p, stroke, ratio); }
        QPainter p(&image);
        p.setCompositionMode(QPainter::CompositionMode_DestinationOut); p.drawImage(0, 0, mask);
        p.setCompositionMode(QPainter::CompositionMode_Plus); p.drawImage(0, 0, layer);
    }
    image.setDevicePixelRatio(ratio);
    return image;
}
}
