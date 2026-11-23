#ifndef CAPTURER_IMAGE_WINDOW_H
#define CAPTURER_IMAGE_WINDOW_H

#include "framelesswindow.h"
#include "TextureWidget.h"
#include "PinSession.h"

#include <memory>
#include <optional>
#include <QMimeData>

class QMenu;
class GlobalSettings;
class PinAnnotationEditor;

class ImageWindow final : public FramelessWindow
{
    Q_OBJECT

public:
    explicit ImageWindow(const std::shared_ptr<QMimeData>& data, QWidget *parent = nullptr,
                         GlobalSettings *settings = nullptr);
    ~ImageWindow() override;

    void preview(const std::shared_ptr<QMimeData>& mimedata);

    std::optional<QPixmap> render(const std::shared_ptr<QMimeData>& mimedata);

    void present(const QPixmap& pixmap);

    static bool is_supported(const QMimeData *);

    static ImageWindow *fromSession(const PinnedImageState &state, GlobalSettings *settings = nullptr);
    PinnedImageState sessionState() const;

    void closeSelf() override;
    void setClosedPinRestoreAvailable(bool available);

signals:
    void restoreClosedPinRequested();

public slots:
    void paste();
    void open();
    void saveAs();
    void copyImage();
    void resetView();
    void restoreOriginal();
    void fitToScreen();
    void toggleThumbnail();
    void setTransparentInput(bool enabled) override;
    void setAnnotationMode(bool editing);
    void undoAnnotation();
    void redoAnnotation();

private:
    void mouseDoubleClickEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

    void wheelEvent(QWheelEvent *) override;
    void contextMenuEvent(QContextMenuEvent *) override;
    void dropEvent(QDropEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;

    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void moveEvent(QMoveEvent *) override;
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;

    void registerShortcuts();
    void initContextMenu();

    void switchThumbnail(bool thumbnail, const QPoint &pos);
    void ensureAnnotationEditor();
    QPixmap compositedPixmap() const;
    void rotateImage(qreal degrees);
    void flipImage(bool horizontal);
    void grayscaleImage();
    PinAnnotationEditor *annotation_editor_{nullptr};
    bool annotation_mode_{false};
    QAction *annotation_action_{nullptr};
    QAction *undo_annotation_action_{nullptr};
    QAction *redo_annotation_action_{nullptr};
    void setZoom(qreal zoom, const QPoint &anchor);
    void setOpacity(qreal opacity);
    void updateGeometryForImage();
    void updateTexture();
    void applyWindowOpacity();
    void showViewHint(const QString &message = QString());
    void runMouseAction(const QString &action, const QPoint &position);

    bool  thumbnail_{ false };
    qreal scale_{ 1.0 };
    qreal opacity_{ 1.0 };

    bool applyingGeometry_{false};
    bool draggingOpacity_{false};
    GlobalSettings *settings_;

    QMenu   *context_menu_{ nullptr };
    QAction *restore_closed_action_{nullptr};
    QAction *zoom_action_{ nullptr };
    QAction *opacity_action_{ nullptr };

    // data
    std::shared_ptr<QMimeData> data_{};
    QPixmap                    pixmap_{};
    QPixmap                    original_pixmap_{};
    TextureWidget             *texture_{};

    QRect thumbnail_source_;
};

#endif //! CAPTURER_IMAGE_WINDOW_H
