#pragma once
#include "PinAnnotationDocument.h"
#include "editor/GraphicsTool.h"
#include <QGraphicsView>
#include <QHash>
#include <memory>

class EditingMenu;
class PinAnnotationEditor final : public QGraphicsView {
    Q_OBJECT
public:
    explicit PinAnnotationEditor(QWidget *parent = nullptr);
    ~PinAnnotationEditor() override;
    PinAnnotationDocument *document() { return &document_; }
    const PinAnnotationDocument *document() const { return &document_; }
    void setEditing(bool active);
    void updateToolbarPosition();
    void finishInteraction();
    bool hasInteraction() const;
signals:
    void annotationsChanged();
    void editingFinished();
    void copyRequested();
    void saveRequested();
protected:
    bool event(QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void drawBackground(QPainter *, const QRectF &) override;
private:
    struct Record;
    using RecordPtr = std::shared_ptr<Record>;
    RecordPtr makeRecord(const PinAnnotation &);
    RecordPtr recordAt(const QPoint &) const;
    RecordPtr focused() const;
    void retire(const RecordPtr &);
    void commit(const RecordPtr &);
    void finishCreation();
    void finishText();
    void rebuild();
    void scheduleRebuild();
    void synchronizeTools();
    void updateSelectedStyle();
    void fitImage();
    void updateCursor(ResizerLocation location = ResizerLocation::DEFAULT);
    QStringList selectedIds() const;
    PinAnnotation style(const QString &type) const;
    QImage effectPreview(const QString &through) const;
    qreal ratio() const;
    PinAnnotationDocument document_;
    EditingMenu *toolbar_{};
    QHash<QString, RecordPtr> records_;
    RecordPtr creating_;
    RecordPtr editing_text_;
    bool active_{false};
    bool syncing_{false};
    bool rebuilding_{false};
    bool fitting_{false};
    bool destroying_{false};
    bool finishing_{false};
    bool selection_mode_{true};
    QString selected_;
};
