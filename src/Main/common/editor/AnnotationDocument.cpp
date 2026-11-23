#include "AnnotationDocument.h"
#include "GraphicsTool.h"
#include <QPainter>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextCursor>
#include <QJsonObject>
#include <QBuffer>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {
QJsonArray matrixJson(const QTransform &m)
{
    return {m.m11(), m.m12(), m.m21(), m.m22(), m.dx(), m.dy()};
}
bool finitePoint(const QPointF &p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y()) && std::abs(p.x()) < 1e7 && std::abs(p.y()) < 1e7;
}
}

class AnnotationCommand final : public QUndoCommand
{
public:
    AnnotationCommand(AnnotationDocument *document, QImage base, QList<annotation::Annotation> items, const QString &text)
        : QUndoCommand(text), document_(document), beforeBase_(document->base_), afterBase_(std::move(base)),
          before_(document->items_), after_(std::move(items)) {}
    void undo() override { apply(beforeBase_, before_); }
    void redo() override { apply(afterBase_, after_); }
private:
    void apply(const QImage &base, const QList<annotation::Annotation> &items)
    {
        document_->base_ = base;
        document_->items_ = items;
        document_->composite_cache_ = {};
        emit document_->changed();
    }
    AnnotationDocument *document_;
    QImage beforeBase_, afterBase_;
    QList<annotation::Annotation> before_, after_;
};

AnnotationDocument::AnnotationDocument(QObject *parent) : QObject(parent), undo_(this)
{
    undo_.setUndoLimit(100);
}

void AnnotationDocument::load(const QImage &base, const QJsonArray &annotations)
{
    undo_.clear();
    base_ = base;
    composite_cache_ = {};
    items_.clear();
    const QStringList types{"rectangle", "ellipse", "arrow", "line", "pen", "text", "mosaic", "eraser", "image", "counter"};
    for (const auto &value : annotations) {
        if (items_.size() >= 2000) break;
        const auto object = value.toObject();
        annotation::Annotation a;
        a.type = object.value("type").toString();
        a.toolVersion = std::clamp(object.value("toolVersion").toInt(0), 0, 1);
        if (!types.contains(a.type)) continue;
        const auto points = object.value("points").toArray();
        if (points.isEmpty() || points.size() > 20000) continue;
        bool valid = true;
        for (const auto &point : points) {
            const auto pair = point.toArray();
            if (pair.size() != 2 || !pair[0].isDouble() || !pair[1].isDouble()) { valid = false; break; }
            QPointF p(pair[0].toDouble(), pair[1].toDouble());
            if (!finitePoint(p)) { valid = false; break; }
            a.points.push_back(p);
        }
        if (!valid || (a.type != "text" && a.points.size() < 2)) continue;
        a.counter = std::max(1, object.value("counter").toInt(1));
        if (a.type == "image") {
            a.image.loadFromData(QByteArray::fromBase64(object.value("imagePng").toString().toLatin1()), "PNG");
            if (a.image.isNull()) continue;
            const auto imageRatio = object.value("imageRatio").toDouble(1);
            a.image.setDevicePixelRatio(std::isfinite(imageRatio) && imageRatio > 0 ? imageRatio : 1);
        }
        a.id = object.value("id").toString();
        if (a.id.isEmpty() || std::any_of(items_.begin(), items_.end(), [&](const auto &item) { return item.id == a.id; }))
            a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.color = QColor(object.value("color").toString());
        if (!a.color.isValid()) a.color = Qt::red;
        const auto width = object.value("width").toDouble(3);
        a.width = std::isfinite(width) ? std::clamp(width, 1.0, 200.0) : 3;
        const auto mosaicSize = object.value("mosaicSize").toDouble(10);
        a.mosaicSize = std::isfinite(mosaicSize) ? std::clamp(mosaicSize, 2.0, 256.0) : 10;
        a.filled = object.value("filled").toBool();
        a.font.fromString(object.value("font").toString());
        const int fontPixels = object.value("fontPixels").toInt(20);
        if (fontPixels > 0) a.font.setPixelSize(std::clamp(fontPixels, 4, 500));
        else {
            const qreal pointSize = object.value("fontPointSize").toDouble(a.font.pointSizeF());
            a.font.setPointSizeF(std::isfinite(pointSize) ? std::clamp(pointSize, 0.5, 500.0) : 20);
        }
        a.text = object.value("text").toString().left(100000);
        a.richText = object.value("richText").toString().left(1000000);
        a.angle = object.value("angle").toDouble();
        if (!std::isfinite(a.angle) || std::abs(a.angle) >= 1e7) a.angle = 0;
        const auto matrix = object.value("transform").toArray();
        if (matrix.size() == 6) {
            bool finite = true;
            for (const auto &n : matrix) finite &= n.isDouble() && std::isfinite(n.toDouble()) && std::abs(n.toDouble()) < 1e7;
            if (!finite) continue;
            a.transform = QTransform(matrix[0].toDouble(), matrix[1].toDouble(), matrix[2].toDouble(),
                                     matrix[3].toDouble(), matrix[4].toDouble(), matrix[5].toDouble());
            if (!a.transform.isInvertible()) continue;
        }
        items_.push_back(a);
    }
    emit changed();
}

QJsonArray AnnotationDocument::serialize() const
{
    QJsonArray result;
    for (const auto &a : items_) {
        QJsonArray points;
        for (const auto &p : a.points) points.append(QJsonArray{p.x(), p.y()});
        QJsonObject object{{"id", a.id}, {"type", a.type}, {"toolVersion", a.toolVersion}, {"points", points},
            {"color", a.color.name(QColor::HexArgb)}, {"width", a.width}, {"mosaicSize", a.mosaicSize}, {"filled", a.filled},
            {"richText", a.richText}, {"angle", a.angle}, {"font", a.font.toString()}, {"fontPixels", a.font.pixelSize()}, {"fontPointSize", a.font.pointSizeF()}, {"text", a.text},
            {"transform", matrixJson(a.transform)}};
        if (a.type == "counter") object.insert("counter", a.counter);
        if (a.type == "image") {
            QByteArray png;
            QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
            a.image.save(&buffer, "PNG");
            object.insert("imagePng", QString::fromLatin1(png.toBase64()));
            object.insert("imageRatio", a.image.devicePixelRatio());
        }
        result.append(object);
    }
    return result;
}

void AnnotationDocument::paint(QPainter &p, const annotation::Annotation &a) { annotation::paint(p, a); }
QImage AnnotationDocument::render(const QImage &base, const QList<annotation::Annotation> &items) { return annotation::render(base, items); }

QImage AnnotationDocument::composite() const
{
    if (composite_cache_.isNull() && !base_.isNull()) composite_cache_ = render(base_, items_);
    return composite_cache_;
}

void AnnotationDocument::commit(QImage base, QList<annotation::Annotation> items, const QString &text)
{
    if (base == base_ && items == items_) return;
    undo_.push(new AnnotationCommand(this, std::move(base), std::move(items), text));
}
void AnnotationDocument::add(annotation::Annotation a)
{
    if (a.points.isEmpty()) return;
    if (a.id.isEmpty()) a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto next = items_;
    next.push_back(std::move(a));
    commit(base_, next, tr("Add Annotation"));
}
void AnnotationDocument::update(const annotation::Annotation &a)
{
    auto next = items_;
    for (auto &item : next) if (item.id == a.id) { item = a; commit(base_, next, tr("Edit Annotation")); return; }
}
void AnnotationDocument::remove(const QStringList &ids)
{
    auto next = items_;
    next.removeIf([&](const auto &item) { return ids.contains(item.id); });
    commit(base_, next, tr("Delete Annotation"));
}
void AnnotationDocument::transformImage(const QTransform &transform)
{
    if (base_.isNull()) return;
    const auto matrix = QImage::trueMatrix(transform, base_.width(), base_.height());
    auto next = items_;
    for (auto &item : next) item.transform *= matrix;
    commit(base_.transformed(transform, Qt::SmoothTransformation), next, tr("Transform Image"));
}
void AnnotationDocument::grayscaleImage()
{
    if (base_.isNull()) return;
    QImage base = base_.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < base.height(); ++y) {
        auto *row = reinterpret_cast<QRgb *>(base.scanLine(y));
        for (int x = 0; x < base.width(); ++x) { const auto pixel = row[x]; const int gray = qGray(pixel); row[x] = qRgba(gray, gray, gray, qAlpha(pixel)); }
    }
    auto next = items_;
    for (auto &item : next) {
        if (!item.image.isNull()) {
            item.image = item.image.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < item.image.height(); ++y) {
                auto *row = reinterpret_cast<QRgb *>(item.image.scanLine(y));
                for (int x = 0; x < item.image.width(); ++x) {
                    const auto pixel = row[x]; const int value = qGray(pixel);
                    row[x] = qRgba(value, value, value, qAlpha(pixel));
                }
            }
        }
        const int gray = qGray(item.color.rgb());
        item.color = QColor(gray, gray, gray, item.color.alpha());
        if (!item.richText.isEmpty()) {
            QTextDocument text;
            text.setHtml(item.richText);
            struct Run { int start, length; QColor color; };
            QList<Run> runs;
            for (auto block = text.begin(); block.isValid(); block = block.next())
                for (auto fragment = block.begin(); !fragment.atEnd(); ++fragment) {
                    const auto value = fragment.fragment();
                    if (value.isValid() && value.charFormat().hasProperty(QTextFormat::ForegroundBrush))
                        runs.append({value.position(), value.length(), value.charFormat().foreground().color()});
                }
            for (const auto &run : runs) {
                QTextCursor cursor(&text);
                cursor.setPosition(run.start); cursor.setPosition(run.start + run.length, QTextCursor::KeepAnchor);
                const int value = qGray(run.color.rgb());
                QTextCharFormat format; format.setForeground(QColor(value, value, value, run.color.alpha()));
                cursor.mergeCharFormat(format);
            }
            item.richText = text.toHtml();
        }
    }
    commit(base, next, tr("Grayscale Image"));
}
