#include "PinAnnotationEditor.h"
#include "TextureWidget.h"
#include "menu/EditingMenu.h"
#include "canvas/canvas.h"
#include "circlecursor.h"
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QPainter>
#include <QScreen>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextCursor>
#include <QInputMethod>
#include <QGuiApplication>
#include <QTimer>
#include <QScopedValueRollback>
#include <QUuid>
#include <algorithm>

struct PinAnnotationEditor::Record {
    PinAnnotation model;
    PinAnnotation baseline;
    QSize canvas;
    QList<PinAnnotation> previewValues;
    QImage previewImage;
    qint64 previewBaseKey{0};
    std::unique_ptr<annotation::GraphicsTool> tool;
};

PinAnnotationEditor::PinAnnotationEditor(QWidget *parent) : QGraphicsView(parent), document_(this)
{
    setScene(new canvas::Canvas(this));
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    toolbar_ = new EditingMenu(this, EditingMenu::GRAPH_GROUP | EditingMenu::REDO_UNDO_GROUP,
                               EditingMenu::Mode::Pin);
    connect(toolbar_, &EditingMenu::imageArrived, this, [this](const QPixmap &pixmap) {
        if (pixmap.isNull()) return;
        finishInteraction();
        auto a = style("image");
        a.image = pixmap.toImage();
        QRectF bounds(QPointF(), pixmap.deviceIndependentSize());
        bounds.moveCenter(sceneRect().center());
        a.points = {bounds.topLeft() * ratio(), bounds.bottomRight() * ratio()};
        document_.add(a);
        toolbar_->paintGraph(canvas::none);
        rebuild();
        if (const auto record = records_.value(a.id)) {
            record->tool->item()->setSelected(true);
            record->tool->item()->setFocus(Qt::OtherFocusReason);
        }
    });
    connect(toolbar_, &EditingMenu::save, this, [this] { finishInteraction(); emit saveRequested(); });
    connect(toolbar_, &EditingMenu::copy, this, [this] { finishInteraction(); emit copyRequested(); });
    connect(toolbar_, &EditingMenu::done, this, [this] { finishInteraction(); emit editingFinished(); });
    connect(toolbar_, &EditingMenu::undo, this, [this] { finishInteraction(); document_.undoStack()->undo(); });
    connect(toolbar_, &EditingMenu::redo, this, [this] { finishInteraction(); document_.undoStack()->redo(); });
    connect(document_.undoStack(), &QUndoStack::canUndoChanged, toolbar_, &EditingMenu::canUndo);
    connect(document_.undoStack(), &QUndoStack::canRedoChanged, toolbar_, &EditingMenu::canRedo);
    toolbar_->canUndo(false); toolbar_->canRedo(false);
    connect(toolbar_, &EditingMenu::graphChanged, this, [this](canvas::graphics_t graph) {
        if (!syncing_) {
            const QScopedValueRollback guard(syncing_, true);
            finishInteraction();
            selection_mode_ = graph == canvas::none;
            scene()->setFocusItem(nullptr); scene()->clearSelection(); selected_.clear();
            for (const auto &record : records_) record->tool->selectBrush(selection_mode_);
        }
        updateCursor();
    });
    connect(toolbar_, &EditingMenu::penChanged, this, [this] { updateSelectedStyle(); });
    connect(toolbar_, &EditingMenu::fontChanged, this, [this] { updateSelectedStyle(); });
    connect(toolbar_, &EditingMenu::fillChanged, this, [this] { updateSelectedStyle(); });
    connect(scene(), &QGraphicsScene::selectionChanged, this, &PinAnnotationEditor::synchronizeTools);
    connect(scene(), &QGraphicsScene::focusItemChanged, this, [this] { synchronizeTools(); });
    connect(&document_, &PinAnnotationDocument::changed, this, [this] {
        if (destroying_) return;
        scheduleRebuild(); emit annotationsChanged();
    });
}
PinAnnotationEditor::~PinAnnotationEditor()
{
    destroying_ = true; active_ = false;
    disconnect(scene(), nullptr, this, nullptr);
    disconnect(&document_, nullptr, this, nullptr);
    disconnect(toolbar_, nullptr, this, nullptr);
    // Each Tool owns its native item. Detach before the scene is destroyed.
    for (const auto &record : records_) {
        if (const auto *text = record->tool->textItem()) disconnect(text->document(), nullptr, this, nullptr);
        scene()->removeItem(record->tool->item());
    }
    creating_.reset(); editing_text_.reset(); records_.clear();
    delete toolbar_; toolbar_ = nullptr;
}
qreal PinAnnotationEditor::ratio() const { return std::max(0.01, document_.baseImage().devicePixelRatio()); }

PinAnnotationEditor::RecordPtr PinAnnotationEditor::makeRecord(const PinAnnotation &value)
{
    auto record = std::make_shared<Record>();
    record->model = value;
    record->canvas = document_.baseImage().size();
    record->tool = std::make_unique<annotation::GraphicsTool>(value, document_.baseImage());
    record->baseline = record->tool->capture();
    auto *item = record->tool->item();
    if (!item) return {};
    records_.insert(value.id, record);
    const std::weak_ptr<Record> weak = record;
    auto *graphics = record->tool->graphics();
    graphics->onhovered([this](auto location) { if (!destroying_) updateCursor(location); });
    graphics->onmoved([this, weak](auto) { if (!destroying_) if (auto r = weak.lock()) commit(r); });
    graphics->onresized([this, weak](auto, auto) { if (!destroying_) if (auto r = weak.lock()) { commit(r); synchronizeTools(); } });
    graphics->onrotated([this, weak](auto) { if (!destroying_) if (auto r = weak.lock()) commit(r); });
    record->tool->selectBrush(selection_mode_);
    if (annotation::isEffect(value.type)) record->tool->effectPreview([this, id = value.id] { return effectPreview(id); });
    scene()->addItem(item);
    if (auto *text = record->tool->textItem())
        connect(text->document(), &QTextDocument::contentsChanged, this, [this] { if (!destroying_) viewport()->update(); });
    return record;
}
PinAnnotationEditor::RecordPtr PinAnnotationEditor::recordAt(const QPoint &point) const
{
    auto *item = itemAt(point);
    return item ? records_.value(item->data(0).toString()) : RecordPtr();
}
PinAnnotationEditor::RecordPtr PinAnnotationEditor::focused() const
{
    if (editing_text_) return editing_text_;
    if (auto *item = scene()->focusItem()) {
        if (auto record = records_.value(item->data(0).toString()); record &&
            (record == creating_ || record->model.id == selected_ || item->isSelected())) return record;
    }
    for (auto *item : scene()->selectedItems())
        if (auto record = records_.value(item->data(0).toString())) return record;
    return {};
}
void PinAnnotationEditor::retire(const RecordPtr &record)
{
    if (!record) return;
    if (const auto *text = record->tool->textItem()) disconnect(text->document(), nullptr, this, nullptr);
    scene()->removeItem(record->tool->item());
    records_.remove(record->model.id);
    // Keep the item alive through any native/scene event currently dispatching.
    QTimer::singleShot(0, this, [record] {});
}
void PinAnnotationEditor::commit(const RecordPtr &record)
{
    if (!record || record == creating_ || record == editing_text_) return;
    const auto current = record->tool->capture();
    if (current == record->baseline) return;
    record->baseline = current;
    document_.update(current);
}
void PinAnnotationEditor::setEditing(bool active)
{
    active_ = active;
    if (active) {
        selection_mode_ = true;
        rebuild();
        for (const auto &record : records_) record->tool->selectBrush(true);
        toolbar_->canUndo(document_.undoStack()->canUndo()); toolbar_->canRedo(document_.undoStack()->canRedo());
        toolbar_->show(); updateToolbarPosition(); setFocus();
    } else {
        finishInteraction();
        const QScopedValueRollback guard(syncing_, true);
        selection_mode_ = true;
        scene()->setFocusItem(nullptr); scene()->clearSelection();
        toolbar_->paintGraph(canvas::none);
        for (auto *submenu : toolbar_->findChildren<EditingSubmenu *>()) submenu->hide();
        toolbar_->hide();
    }
    updateCursor();
}
void PinAnnotationEditor::updateToolbarPosition()
{
    if (!toolbar_) return;
    if (!active_ || !isVisible()) { toolbar_->hide(); return; }
    const QRect available = screen() ? screen()->availableGeometry() : QRect(mapToGlobal(QPoint()), QSize(1920, 1080));
    const auto bottom = mapToGlobal(QPoint(width(), height()));
    const int x = std::clamp(bottom.x() - toolbar_->width(), available.left(), std::max(available.left(), available.right() - toolbar_->width()));
    int y = bottom.y() + 6;
    const bool above = y + toolbar_->height() * 2 > available.bottom();
    if (above) y = mapToGlobal(QPoint()).y() - toolbar_->height() - 6;
    y = std::clamp(y, available.top(), std::max(available.top(), available.bottom() - toolbar_->height()));
    toolbar_->setSubMenuShowAbove(above); toolbar_->move(x, y); toolbar_->show();
}
void PinAnnotationEditor::scheduleRebuild()
{
    if (rebuilding_ || destroying_) return;
    rebuilding_ = true;
    QTimer::singleShot(0, this, [this] { rebuilding_ = false; rebuild(); });
}
void PinAnnotationEditor::rebuild()
{
    if (destroying_) return;
    {
        const QScopedValueRollback guard(syncing_, true);
        scene()->setSceneRect(QRectF(QPointF(), document_.baseImage().size() / ratio()));
        auto obsolete = records_;
        int index = 1;
        for (const auto &value : document_.items()) {
            auto record = records_.value(value.id);
            if (record && (record->model.type != value.type || record->canvas != document_.baseImage().size() || record->tool->ratio() != ratio())) { retire(record); record.reset(); }
            if (!record) record = makeRecord(value);
            if (!record) continue;
            if (record != editing_text_ && record != creating_ && record->model != value) {
                record->tool->apply(value); record->model = value; record->baseline = record->tool->capture();
            }
            record->tool->item()->setZValue(index++);
            obsolete.remove(value.id);
        }
        if (creating_) { obsolete.remove(creating_->model.id); creating_->tool->item()->setZValue(index++); }
        if (editing_text_) { obsolete.remove(editing_text_->model.id); editing_text_->tool->item()->setZValue(index); }
        for (const auto &record : obsolete) retire(record);
    }
    synchronizeTools(); fitImage(); viewport()->update();
}
QStringList PinAnnotationEditor::selectedIds() const
{
    QStringList ids;
    for (auto *item : scene()->selectedItems()) if (records_.contains(item->data(0).toString())) ids.append(item->data(0).toString());
    if (const auto record = focused(); record && !ids.contains(record->model.id)) ids.append(record->model.id);
    return ids;
}
PinAnnotation PinAnnotationEditor::style(const QString &type) const
{
    PinAnnotation a; a.id = QUuid::createUuid().toString(QUuid::WithoutBraces); a.type = type;
    a.color = toolbar_->pen().color(); a.width = toolbar_->pen().widthF(); a.filled = toolbar_->filled();
    a.font = toolbar_->font();
    const qreal pixels = a.font.pixelSize() > 0 ? a.font.pixelSize() : std::max(1.0, a.font.pointSizeF()) * logicalDpiY() / 72;
    a.font.setPixelSize(std::clamp(qRound(pixels * ratio()), 4, 500));
    return a;
}
void PinAnnotationEditor::synchronizeTools()
{
    if (syncing_ || !active_ || destroying_) return;
    const auto record = focused();
    if (!record) { selected_.clear(); return; }
    const auto a = record->tool->capture(); selected_ = a.id;
    const QScopedValueRollback guard(syncing_, true);
    toolbar_->paintGraph(a.type == "image" ? canvas::none : annotation::graph(a.type));
    toolbar_->setPen(QPen(a.color, a.width));
    if (record->tool->textItem()) toolbar_->setFont(record->tool->graphics()->font());
    toolbar_->fill(a.filled);
}
void PinAnnotationEditor::updateSelectedStyle()
{
    if (syncing_ || destroying_) return;
    if (const auto record = focused()) {
        record->tool->changeStyle(toolbar_->pen(), toolbar_->font(), toolbar_->filled(), record->model.mosaicSize);
        commit(record);
        viewport()->update();
    }
    updateCursor();
}
bool PinAnnotationEditor::hasInteraction() const { return creating_ || editing_text_; }
void PinAnnotationEditor::finishCreation()
{
    if (!creating_) return;
    const auto record = creating_; creating_.reset();
    record->tool->finish(); record->tool->stopGesture();
    if (record->tool->invalid()) { retire(record); return; }
    const auto a = record->tool->capture(); record->baseline = a;
    document_.add(a);
}
void PinAnnotationEditor::finishText()
{
    if (!editing_text_) return;
    if (auto *input = QGuiApplication::inputMethod()) input->commit();
    const auto record = editing_text_; editing_text_.reset();
    const bool newText = creating_ == record;
    if (newText) creating_.reset();
    auto *text = record->tool->textItem();
    text->setTextInteractionFlags(Qt::NoTextInteraction);
    auto cursor = text->textCursor(); cursor.clearSelection(); text->setTextCursor(cursor);
    const auto a = record->tool->capture();
    if (a.text.trimmed().isEmpty()) {
        if (!newText) document_.remove({a.id});
        retire(record);
    } else if (newText) { record->baseline = a; document_.add(a); }
    else commit(record);
    viewport()->update();
}
void PinAnnotationEditor::finishInteraction()
{
    if (finishing_ || destroying_) return;
    const QScopedValueRollback guard(finishing_, true);
    finishText(); finishCreation();
    if (!rebuilding_) for (const auto &record : records_) { record->tool->stopGesture(); commit(record); }
}
QImage PinAnnotationEditor::effectPreview(const QString &through) const
{
    QList<PinAnnotation> values;
    bool found = false;
    for (const auto &a : document_.items()) {
        const auto record = records_.value(a.id);
        values.append(record ? record->tool->capture() : a);
        if (a.id == through) { found = true; break; }
    }
    if (!found && creating_) values.append(creating_->tool->capture());
    const auto record = records_.value(through);
    const auto key = document_.baseImage().cacheKey();
    if (record && record->previewBaseKey == key && record->previewValues == values) return record->previewImage;
    auto image = annotation::render(document_.baseImage(), values);
    if (record) { record->previewBaseKey = key; record->previewValues = values; record->previewImage = image; }
    return image;
}
void PinAnnotationEditor::drawBackground(QPainter *p, const QRectF &)
{
    p->drawImage(sceneRect(), document_.baseImage());
}
bool PinAnnotationEditor::event(QEvent *event)
{
    if (active_ && event->type() == QEvent::ShortcutOverride) { event->accept(); return true; }
    return QGraphicsView::event(event);
}
void PinAnnotationEditor::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event); fitImage(); updateToolbarPosition();
}
void PinAnnotationEditor::mousePressEvent(QMouseEvent *event)
{
    const auto keepAlive = records_.values();
    if (event->button() == Qt::RightButton) { finishInteraction(); scene()->setFocusItem(nullptr); scene()->clearSelection(); event->accept(); return; }
    if (event->button() != Qt::LeftButton) { event->accept(); return; }
    const auto hit = recordAt(event->position().toPoint());
    if (editing_text_ && hit != editing_text_) finishText();
    const auto graph = selection_mode_ ? canvas::none : toolbar_->graph();
    const QString type = annotation::type(graph);
    const auto point = mapToScene(event->position().toPoint());
    if (hit && (!annotation::isEffect(type) || selection_mode_)) {
        hit->tool->preparePress(point);
        if (hit->tool->graphics()->location(hit->tool->item()->mapFromScene(point)) != ResizerLocation::EMPTY_INSIDE) {
            QGraphicsView::mousePressEvent(event); return;
        }
    }
    if (!sceneRect().contains(point) || type.isEmpty() || type == "image") { scene()->setFocusItem(nullptr); scene()->clearSelection(); event->accept(); return; }
    finishInteraction();
    {
        const QScopedValueRollback guard(syncing_, true);
        scene()->setFocusItem(nullptr); scene()->clearSelection();
    }
    auto a = style(type); a.points = (type == "text" || type == "counter") ? QList<QPointF>{point * ratio()} : QList<QPointF>{point * ratio(), point * ratio()};
    if (type == "counter") {
        for (const auto &item : document_.items()) if (item.type == "counter") a.counter = std::max(a.counter, item.counter + 1);
    }
    creating_ = makeRecord(a);
    if (!creating_) return;
    creating_->tool->item()->setZValue(document_.items().size() + 1);
    if (type == "text") {
        editing_text_ = creating_;
        auto *text = creating_->tool->textItem();
        text->setObjectName("pin-inline-text");
        text->setTextInteractionFlags(Qt::TextEditorInteraction);
        text->document()->clearUndoRedoStacks();
    }
    creating_->tool->item()->setFocus(Qt::MouseFocusReason);
    viewport()->update(); event->accept();
}
void PinAnnotationEditor::mouseMoveEvent(QMouseEvent *event)
{
    const auto keepAlive = records_.values();
    if (creating_ && creating_ != editing_text_) {
        creating_->tool->push(mapToScene(event->position().toPoint()));
        viewport()->update(); event->accept();
    } else { QGraphicsView::mouseMoveEvent(event); viewport()->update(); }
}
void PinAnnotationEditor::mouseReleaseEvent(QMouseEvent *event)
{
    const auto keepAlive = records_.values();
    if (creating_ && creating_ != editing_text_) { finishCreation(); viewport()->update(); event->accept(); }
    else QGraphicsView::mouseReleaseEvent(event);
}
void PinAnnotationEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    const auto keepAlive = records_.values();
    const auto record = recordAt(event->position().toPoint());
    if (event->button() == Qt::LeftButton && record && record->tool->textItem()) {
        if (editing_text_ && editing_text_ != record) finishText();
        editing_text_ = record;
        record->tool->preparePress(mapToScene(event->position().toPoint()));
        QGraphicsView::mouseDoubleClickEvent(event);
        record->tool->textItem()->setTextInteractionFlags(Qt::TextEditorInteraction);
        record->tool->item()->setFocus(Qt::MouseFocusReason);
        synchronizeTools();
    } else QGraphicsView::mouseDoubleClickEvent(event);
}
void PinAnnotationEditor::keyPressEvent(QKeyEvent *event)
{
    const auto keepAlive = records_.values();
    if (!editing_text_) {
        const auto record = focused();
        if (record && record->tool->textItem() && record->tool->textItem()->textInteractionFlags() == Qt::TextEditorInteraction) editing_text_ = record;
    }
    if (editing_text_) {
        if (event->key() == Qt::Key_Escape || ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers().testFlag(Qt::ControlModifier))) {
            finishText(); event->accept();
        } else if (event->matches(QKeySequence::Save)) { finishInteraction(); emit saveRequested(); event->accept(); }
        else {
            editing_text_->tool->textItem()->setTextInteractionFlags(Qt::TextEditorInteraction);
            scene()->setFocusItem(editing_text_->tool->item());
            QGraphicsView::keyPressEvent(event);
        }
        return;
    }
    if (event->matches(QKeySequence::Undo)) { finishInteraction(); document_.undoStack()->undo(); }
    else if (event->matches(QKeySequence::Redo) || (event->key() == Qt::Key_Z && event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))) { finishInteraction(); document_.undoStack()->redo(); }
    else if (event->matches(QKeySequence::Copy)) { finishInteraction(); emit copyRequested(); }
    else if (event->matches(QKeySequence::Save)) { finishInteraction(); emit saveRequested(); }
    else if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) { finishInteraction(); document_.remove(selectedIds()); }
    else if (event->key() == Qt::Key_Space || event->key() == Qt::Key_Escape) { finishInteraction(); emit editingFinished(); }
    else { QGraphicsView::keyPressEvent(event); return; }
    event->accept();
}
void PinAnnotationEditor::wheelEvent(QWheelEvent *event)
{
    if (!event->angleDelta().y()) { event->ignore(); return; }
    const int delta = event->angleDelta().y() / 120;
    const auto graph = toolbar_->graph();
    auto pen = toolbar_->pen();
    if (event->modifiers().testFlag(Qt::ControlModifier) && canvas::has_color(graph)) {
        auto color = pen.color(); color.setAlpha(std::clamp(color.alpha() + delta * 5, 0, 255)); pen.setColor(color); toolbar_->setPen(pen, false);
    } else if (canvas::has_width(graph)) { pen.setWidth(std::clamp(pen.width() + delta, 1, 71)); toolbar_->setPen(pen, false); }
    updateCursor(); event->accept();
}
void PinAnnotationEditor::updateCursor(ResizerLocation location)
{
    // Focus/hover callbacks can still arrive while the editor is being hidden.
    // They must never reapply a drawing cursor after editing has ended.
    if (!active_) { viewport()->setCursor(Qt::ArrowCursor); return; }
    if (selection_mode_) { viewport()->setCursor(getCursorByLocation(location, Qt::ArrowCursor)); return; }
    if (toolbar_->graph() == canvas::eraser) viewport()->setCursor(QCursor(cursor::circle(toolbar_->pen().width(), {QColor(136,136,136),3}, Qt::NoBrush)));
    else if (toolbar_->graph() == canvas::mosaic) viewport()->setCursor(QCursor(cursor::circle(toolbar_->pen().width(), {QColor(136,136,136),3})));
    else viewport()->setCursor(getCursorByLocation(location, Qt::CrossCursor));
}
void PinAnnotationEditor::fitImage()
{
    if (fitting_ || document_.baseImage().isNull() || rect().isEmpty()) return;
    const QRect target = TextureWidget::imageRect(document_.baseImage().size(), rect());
    if (target.isEmpty()) return;
    const QScopedValueRollback guard(fitting_, true);
    setViewportMargins(target.left(), target.top(), width() - target.right() - 1, height() - target.bottom() - 1);
    setTransform(QTransform::fromScale(qreal(target.width()) / sceneRect().width(), qreal(target.height()) / sceneRect().height()));
    horizontalScrollBar()->setValue(0); verticalScrollBar()->setValue(0);
}
