#include "ImageWindow.h"

#include "clipboard.h"
#include "GlobalSettings.h"
#include "logging.h"
#include "menu.h"
#include "PinAnnotationEditor.h"

#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QFileDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMessageBox>
#include <QShortcut>
#include <QTextEdit>
#include <QVBoxLayout>

#include <QScreen>
#include <QToolTip>
#include <QInputDialog>
#include <QResizeEvent>
#include <cmath>

#ifdef Q_OS_WIN
#include "platforms/WindowEffect.h"
#endif

ImageWindow::ImageWindow(const std::shared_ptr<QMimeData>& data, QWidget *parent, GlobalSettings *settings)
#ifdef _WIN32
    : FramelessWindow(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
#elif __APPLE__
    : FramelessWindow(parent, Qt::Window | Qt::WindowStaysOnTopHint)
#endif
{
    settings_ = settings ? settings : g_Settings;
    setWindowTitle("Image Window");
    // this->setWindowFlags(this->windowFlags() | Qt::CustomizeWindowHint | Qt::WindowTitleHint);

    setAcceptDrops(true);

#ifdef Q_OS_WIN
    windows::dwm::set_window_corner(reinterpret_cast<HWND>(winId()), DWMWCP_DONOTROUND);
#endif

    texture_ = new TextureWidget(this);
    setLayout(new QVBoxLayout());
    layout()->setSpacing(0);
    layout()->setContentsMargins({});
    layout()->addWidget(texture_);

    if(data.get()) preview(data);

    registerShortcuts();
    initContextMenu();
}

void ImageWindow::preview(const std::shared_ptr<QMimeData>& mimedata)
{
    if (!mimedata) return;
    const auto rendered = render(mimedata);
    if (!rendered || rendered->isNull()) return;
    if (data_ && data_ != mimedata) data_->setData(clipboard::MIME_TYPE_STATUS, "N");
    data_ = mimedata;
    data_->setData(clipboard::MIME_TYPE_STATUS, "P");
    setAnnotationMode(false);
    original_pixmap_ = *rendered;
    pixmap_ = original_pixmap_;
    if (annotation_editor_) annotation_editor_->document()->load(pixmap_.toImage());
    scale_ = 1.0;
    opacity_ = 1.0;
    thumbnail_ = false;
    thumbnail_source_ = {};
    applyWindowOpacity();
    updateTexture();
    updateGeometryForImage();
    if (mimedata->hasFormat(clipboard::MIME_TYPE_POINT))
        move(clipboard::deserialize<QPoint>(mimedata->data(clipboard::MIME_TYPE_POINT)));
}

void ImageWindow::updateTexture()
{
    const auto image = compositedPixmap();
    if (thumbnail_ && thumbnail_source_.isValid()) texture_->present(image.copy(thumbnail_source_));
    else texture_->present(image);
}

void ImageWindow::updateGeometryForImage()
{
    if (pixmap_.isNull()) return;
    QSize target;
    if (thumbnail_) target = QSize(settings_->pinThumbnailSize, settings_->pinThumbnailSize);
    else target = (pixmap_.deviceIndependentSize() * scale_).toSize().expandedTo(QSize(1, 1));
    QRect next(QPoint(), target);
    next.moveCenter(geometry().center());
    applyingGeometry_ = true;
    setGeometry(next);
    applyingGeometry_ = false;
}

void ImageWindow::present(const QPixmap& pixmap)
{
    if (pixmap.isNull()) return;
    setAnnotationMode(false);
    pixmap_ = pixmap;
    if (annotation_editor_) annotation_editor_->document()->load(pixmap_.toImage());
    // An image edit returns to the full image so its result is immediately visible.
    thumbnail_ = false;
    thumbnail_source_ = {};
    updateTexture();
    updateGeometryForImage();
}

void ImageWindow::copyImage()
{
    // Copy the current full-resolution pixels; view zoom/opacity/thumbnail are not image edits.
    if (annotation_editor_) annotation_editor_->finishInteraction();
    if (!pixmap_.isNull()) clipboard::push(compositedPixmap());
}

void ImageWindow::resetView()
{
    scale_ = 1.0;
    opacity_ = 1.0;
    thumbnail_ = false;
    thumbnail_source_ = {};
    updateTexture();
    updateGeometryForImage();
    applyWindowOpacity();
    showViewHint();
}

void ImageWindow::restoreOriginal()
{
    if (original_pixmap_.isNull()) return;
    setAnnotationMode(false);
    pixmap_ = original_pixmap_;
    if (annotation_editor_) annotation_editor_->document()->load(pixmap_.toImage());
    resetView();
}

void ImageWindow::showViewHint(const QString &message)
{
    if (!isVisible()) return;
    const QString text = message.isEmpty()
        ? tr("Zoom: %1%   Opacity: %2%").arg(qRound(scale_ * 100)).arg(qRound(opacity_ * 100)) : message;
    QToolTip::showText(QCursor::pos() + QPoint(12, 16), text, this, QRect(), 1400);
}

void ImageWindow::applyWindowOpacity()
{
    qreal effective = draggingOpacity_ ? settings_->moveopaque : opacity_;
#ifdef Q_OS_WIN
    if (transparent_input_ && effective >= 1.0) effective = 0.99;
#endif
    setWindowOpacity(effective);
}

void ImageWindow::setTransparentInput(bool enabled)
{
    if (enabled && annotation_mode_) setAnnotationMode(false);
    const bool changed = enabled != isTransparentInput();
    FramelessWindow::setTransparentInput(enabled);
    applyWindowOpacity();
    if (changed) showViewHint(enabled ? tr("Click-through enabled") : tr("Click-through disabled"));
}

void ImageWindow::setOpacity(qreal opacity)
{
    if (!std::isfinite(opacity)) return;
    opacity_ = std::clamp(opacity, 0.01, 1.0);
    applyWindowOpacity();
    showViewHint();
}

void ImageWindow::setZoom(qreal zoom, const QPoint &anchor)
{
    if (pixmap_.isNull() || thumbnail_ || !std::isfinite(zoom)) return;
    scale_ = std::clamp(zoom, 0.01, 32.0);
    const auto size = (pixmap_.deviceIndependentSize() * scale_).toSize().expandedTo(QSize(1, 1));
    QRect imageRect(QPoint(), pixmap_.size().scaled(this->size(), Qt::KeepAspectRatio));
    imageRect.moveCenter(rect().center());
    const qreal x = std::clamp(qreal(anchor.x() - imageRect.x()) / std::max(1, imageRect.width()), 0.0, 1.0);
    const qreal y = std::clamp(qreal(anchor.y() - imageRect.y()) / std::max(1, imageRect.height()), 0.0, 1.0);
    const QPoint global = mapToGlobal(anchor);
    applyingGeometry_ = true;
    setGeometry(QRect(global - QPoint(qRound(x * size.width()), qRound(y * size.height())), size));
    applyingGeometry_ = false;
    showViewHint();
}

void ImageWindow::fitToScreen()
{
    if (pixmap_.isNull()) return;
    auto *display = screen();
    if (!display) display = QGuiApplication::primaryScreen();
    if (!display) return;
    thumbnail_ = false;
    thumbnail_source_ = {};
    const QRect available = display->availableGeometry().adjusted(12, 12, -12, -12);
    const auto natural = pixmap_.deviceIndependentSize();
    scale_ = std::min({1.0, available.width() / natural.width(), available.height() / natural.height()});
    updateTexture();
    updateGeometryForImage();
    move(available.center() - rect().center());
    showViewHint();
}

void ImageWindow::resizeEvent(QResizeEvent *event)
{
    FramelessWindow::resizeEvent(event);
    if (annotation_editor_ && annotation_mode_) annotation_editor_->updateToolbarPosition();
    if (!applyingGeometry_ && !thumbnail_ && !pixmap_.isNull()) {
        const auto natural = pixmap_.deviceIndependentSize();
        scale_ = std::min(width() / natural.width(), height() / natural.height());
    }
}

void ImageWindow::runMouseAction(const QString &action, const QPoint &position)
{
    if (action == "thumbnail") switchThumbnail(!thumbnail_, position);
    else if (action == "reset") resetView();
    else if (action == "fit") fitToScreen();
    else if (action == "close") closeSelf();
}

void ImageWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    draggingOpacity_ = false;
    applyWindowOpacity();
    if (event->button() == Qt::LeftButton) {
        runMouseAction(event->modifiers().testFlag(Qt::ShiftModifier)
                       ? settings_->pinShiftDoubleClick : settings_->pinDoubleClick, event->position().toPoint());
        event->accept();
    }
}

void ImageWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        runMouseAction(settings_->pinMiddleClick, event->position().toPoint());
        event->accept();
        return;
    }
    if (dragmove_ && event->button() == Qt::LeftButton) {
        draggingOpacity_ = true;
        applyWindowOpacity();
    }
    FramelessWindow::mousePressEvent(event);
}

void ImageWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        draggingOpacity_ = false;
        applyWindowOpacity();
    }
    FramelessWindow::mouseReleaseEvent(event);
}

void ImageWindow::wheelEvent(QWheelEvent *event)
{
    const qreal steps = event->angleDelta().y() ? event->angleDelta().y() / 120.0 : event->pixelDelta().y() / 40.0;
    if (steps == 0) { event->ignore(); return; }
    if (event->modifiers().testFlag(Qt::ControlModifier)) setOpacity(opacity_ + steps * 0.05);
    else setZoom(scale_ * std::pow(1.05, steps), event->position().toPoint());
    event->accept();
}

void ImageWindow::paste()
{
    preview(std::shared_ptr<QMimeData>(clipboard::clone(QApplication::clipboard()->mimeData())));
}

void ImageWindow::open()
{
    const auto filename = QFileDialog::getOpenFileName(this, tr("Open Image"), settings_->path,
                                                       "Image Files(*.png *.jpg *.jpeg *.bmp *.svg)");
    if (!filename.isEmpty()) {
        auto imageData = std::make_shared<QMimeData>();
        imageData->setImageData(QVariant(QPixmap(filename)));
        preview(imageData);
    }
}

void ImageWindow::saveAs()
{
    const auto target = settings_->selectImageSaveTarget(this, tr("Save Image"));
    if (target.isEmpty()) return;
    if (annotation_editor_) annotation_editor_->finishInteraction();
    if (!settings_->saveImage(compositedPixmap().toImage(), target))
        QMessageBox::warning(this, tr("Save Image"), tr("Failed to save the image. Please check the destination and try again."));
}

// clang-format off
void ImageWindow::initContextMenu()
{
    context_menu_ = new Menu(this);

    annotation_action_ = context_menu_->addAction(tr("Annotate"), [this] { setAnnotationMode(!annotation_mode_); });
    annotation_action_->setCheckable(true);
    undo_annotation_action_ = context_menu_->addAction(tr("Undo Annotation"), this, &ImageWindow::undoAnnotation);
    redo_annotation_action_ = context_menu_->addAction(tr("Redo Annotation"), this, &ImageWindow::redoAnnotation);
    undo_annotation_action_->setEnabled(false);
    redo_annotation_action_->setEnabled(false);
    context_menu_->addSeparator();
    context_menu_->addAction(tr("Close"), QKeySequence(Qt::Key_Escape), this, &ImageWindow::closeSelf);
    addAction(context_menu_->addAction(tr("Close All"), QKeySequence(Qt::CTRL | Qt::Key_Escape), [this]() { emit closeAll(); }));

    restore_closed_action_ = context_menu_->addAction(tr("Restore Last Closed Pin"),
                                                    [this] { emit restoreClosedPinRequested(); });
    restore_closed_action_->setEnabled(false);
    context_menu_->addAction(tr("Reset View (100% / Opaque)"), this, &ImageWindow::resetView);
    context_menu_->addAction(tr("Fit to Screen"), this, &ImageWindow::fitToScreen);
    context_menu_->addAction(tr("Toggle Thumbnail"), this, &ImageWindow::toggleThumbnail);
    context_menu_->addSeparator();
    addAction(context_menu_->addAction(tr("Copy"),  QKeySequence::Copy,     [this] { copyImage(); }));
    // addAction(context_menu_->addAction(tr("Paste"), QKeySequence::Paste,    [this] { paste(); }));

    // context_menu_->addSeparator();

    // addAction(context_menu_->addAction(tr("Open Image..."), QKeySequence::Open, this, &ImageWindow::open));
    addAction(context_menu_->addAction(tr("Save As..."),    QKeySequence::Save, this, &ImageWindow::saveAs));

    context_menu_->addSeparator();

    addAction(context_menu_->addAction(tr("Grayscale"),     QKeySequence(Qt::Key_G),            [this] { grayscaleImage(); }));
    addAction(context_menu_->addAction(tr("Rotate +90"),    QKeySequence(Qt::Key_R),            [this] { rotateImage(90); }));
    addAction(context_menu_->addAction(tr("Rotate -90"),    QKeySequence(Qt::CTRL | Qt::Key_R), [this] { rotateImage(-90); }));
    addAction(context_menu_->addAction(tr("H Flip"),        QKeySequence(Qt::Key_H),            [this] { flipImage(true); }));
    addAction(context_menu_->addAction(tr("V Flip"),        QKeySequence(Qt::Key_V),            [this] { flipImage(false); }));

    context_menu_->addSeparator();

    const auto sub_menu = new Menu(tr("Background"), this);

    const auto group = new QActionGroup(this);
    group->addAction(sub_menu->addAction(tr("White"),        [this] { setStyleSheet("ImageWindow{ background: white; }"); }))->setCheckable(true);
    group->addAction(sub_menu->addAction(tr("Gray"),         [this] { setStyleSheet("ImageWindow{ background: gray; }"); }))->setCheckable(true);
    group->addAction(sub_menu->addAction(tr("Black"),        [this] { setStyleSheet("ImageWindow{ background: black; }"); }))->setCheckable(true);
    group->actions()[settings_->definiteTheme() == "light" ? 0 : 2]->trigger();
    context_menu_->addMenu(sub_menu);

    zoom_action_    = context_menu_->addAction(tr("Zoom : ")    + QString::number(static_cast<int>(scale_ * 100)) + "%");
    opacity_action_ = context_menu_->addAction(tr("Opacity : ") + QString::number(static_cast<int>(opacity_ * 100)) + "%");
    auto *clickThrough = context_menu_->addAction(tr("Transparent Input"));
    clickThrough->setCheckable(true);
    clickThrough->setChecked(isTransparentInput());
    connect(clickThrough, &QAction::triggered, this, &ImageWindow::setTransparentInput);
    connect(this, &FramelessWindow::transparentInputChanged, clickThrough, &QAction::setChecked);
    connect(zoom_action_, &QAction::triggered, this, [this] {
        bool accepted = false;
        const auto percent = QInputDialog::getDouble(this, tr("Zoom"), tr("Percent:"), scale_ * 100, 1, 3200, 1, &accepted);
        if (accepted) { if (thumbnail_) toggleThumbnail(); setZoom(percent / 100, rect().center()); }
    });
    connect(opacity_action_, &QAction::triggered, this, [this] {
        bool accepted = false;
        const auto percent = QInputDialog::getDouble(this, tr("Opacity"), tr("Percent:"), opacity_ * 100, 1, 100, 0, &accepted);
        if (accepted) setOpacity(percent / 100);
    });

    context_menu_->addSeparator();
    context_menu_->addAction(tr("Restore Original Image (Clear Annotations)"), this, &ImageWindow::restoreOriginal);

}

void ImageWindow::toggleThumbnail()
{
    switchThumbnail(!thumbnail_, rect().center());
}

void ImageWindow::switchThumbnail(bool thumbnail, const QPoint &pos)
{
    if (pixmap_.isNull()) return;
    if (annotation_mode_) setAnnotationMode(false);
    if (thumbnail) {
        QRect painted(QPoint(), pixmap_.size().scaled(size(), Qt::KeepAspectRatio));
        painted.moveCenter(rect().center());
        const qreal factor = qreal(pixmap_.width()) / std::max(1, painted.width());
        const QPoint center(qRound((pos.x() - painted.x()) * factor), qRound((pos.y() - painted.y()) * factor));
        const int side = settings_->pinThumbnailSize;
        const QSize sample = (QSizeF(side, side) * factor).toSize().expandedTo(QSize(1, 1)).boundedTo(pixmap_.size());
        thumbnail_source_ = QRect(QPoint(), sample);
        thumbnail_source_.moveCenter(center);
        thumbnail_source_.moveLeft(std::clamp(thumbnail_source_.left(), 0, pixmap_.width() - sample.width()));
        thumbnail_source_.moveTop(std::clamp(thumbnail_source_.top(), 0, pixmap_.height() - sample.height()));
    }
    thumbnail_ = thumbnail;
    updateTexture();
    updateGeometryForImage();
    showViewHint(thumbnail ? tr("Thumbnail mode") : QString());
}
// clang-format on

void ImageWindow::contextMenuEvent(QContextMenuEvent *event)
{
    zoom_action_->setText(tr("Zoom : ") + QString::number(static_cast<int>(scale_ * 100)) + "%");
    opacity_action_->setText(tr("Opacity : ") + QString::number(static_cast<int>(opacity_ * 100)) + "%");

    context_menu_->exec(event->globalPos());
}

void ImageWindow::dropEvent(QDropEvent *event)
{
    preview(std::shared_ptr<QMimeData>(clipboard::clone(event->mimeData())));

    event->acceptProposedAction();
}

void ImageWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (is_supported(event->mimeData())) {
        event->acceptProposedAction();
    }
}

void ImageWindow::closeEvent(QCloseEvent *event)
{
    setAnnotationMode(false);
    if (data_) data_->setData(clipboard::MIME_TYPE_STATUS, "N");
    return FramelessWindow::closeEvent(event);
}

void ImageWindow::closeSelf()
{
    close();
}

void ImageWindow::registerShortcuts()
{
    const auto shortcut = [this](const QKeySequence &key, auto action) {
        connect(new QShortcut(key, this), &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::Key_Space), [this] { setAnnotationMode(!annotation_mode_); });
    shortcut(QKeySequence::Undo, [this] { undoAnnotation(); });
    shortcut(QKeySequence::Redo, [this] { redoAnnotation(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] { redoAnnotation(); });
    shortcut(QKeySequence(Qt::Key_Plus), [this] { setZoom(scale_ * 1.05, rect().center()); });
    shortcut(QKeySequence(Qt::Key_Equal), [this] { setZoom(scale_ * 1.05, rect().center()); });
    shortcut(QKeySequence(Qt::Key_Minus), [this] { setZoom(scale_ / 1.05, rect().center()); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Plus), [this] { setOpacity(opacity_ + 0.05); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal), [this] { setOpacity(opacity_ + 0.05); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus), [this] { setOpacity(opacity_ - 0.05); });
    shortcut(QKeySequence(Qt::Key_0), [this] { resetView(); });
    shortcut(QKeySequence(Qt::Key_F), [this] { fitToScreen(); });
    shortcut(QKeySequence(Qt::Key_T), [this] { toggleThumbnail(); });
    // clang-format off
    connect(new QShortcut(Qt::Key_Escape, this),    &QShortcut::activated, [this] { closeSelf(); });

    // move
    connect(new QShortcut(Qt::Key_W, this),         &QShortcut::activated, [this] { setGeometry(geometry().translated(0, -1)); });
    connect(new QShortcut(Qt::Key_Up, this),        &QShortcut::activated, [this] { setGeometry(geometry().translated(0, -1)); });

    connect(new QShortcut(Qt::Key_S, this),         &QShortcut::activated, [this] { setGeometry(geometry().translated(0, +1)); });
    connect(new QShortcut(Qt::Key_Down, this),      &QShortcut::activated, [this] { setGeometry(geometry().translated(0, +1)); });

    connect(new QShortcut(Qt::Key_A, this),         &QShortcut::activated, [this] { setGeometry(geometry().translated(-1, 0)); });
    connect(new QShortcut(Qt::Key_Left, this),      &QShortcut::activated, [this] { setGeometry(geometry().translated(-1, 0)); });

    connect(new QShortcut(Qt::Key_D, this),         &QShortcut::activated, [this] { setGeometry(geometry().translated(+1, 0)); });
    connect(new QShortcut(Qt::Key_Right, this),     &QShortcut::activated, [this] { setGeometry(geometry().translated(+1, 0)); });
    // clang-format on
}

bool ImageWindow::is_supported(const QMimeData *mimedata)
{
    return mimedata->hasImage() || mimedata->hasText();
}

PinnedImageState ImageWindow::sessionState() const
{
    if (annotation_editor_) annotation_editor_->finishInteraction();
    PinnedImageState state{compositedPixmap().toImage(), geometry(), thumbnail_source_, scale_, opacity_, thumbnail_, styleSheet()};
    if (annotation_editor_ && !annotation_editor_->document()->items().isEmpty()) {
        state.annotationBase = annotation_editor_->document()->baseImage();
        state.annotations = annotation_editor_->document()->serialize();
    }
    state.originalImage = original_pixmap_.toImage();
    state.transparentInput = isTransparentInput();
    return state;
}

ImageWindow *ImageWindow::fromSession(const PinnedImageState &state, GlobalSettings *settings)
{
    if (state.image.isNull()) return nullptr;
    auto restoredData = std::make_shared<QMimeData>();
    restoredData->setImageData(QVariant(QPixmap::fromImage(state.image)));
    auto *window = new ImageWindow(restoredData, nullptr, settings);
    if (!state.originalImage.isNull()) window->original_pixmap_ = QPixmap::fromImage(state.originalImage);
    if (!state.annotationBase.isNull() && !state.annotations.isEmpty()) {
        window->ensureAnnotationEditor();
        window->annotation_editor_->document()->load(state.annotationBase, state.annotations);
    }
    window->scale_ = state.scale;
    window->opacity_ = state.opacity;
    window->thumbnail_ = state.thumbnail;
    window->thumbnail_source_ = state.thumbnailSource.intersected(window->pixmap_.rect());
    if (state.background == "ImageWindow{ background: white; }" ||
        state.background == "ImageWindow{ background: gray; }" ||
        state.background == "ImageWindow{ background: black; }") window->setStyleSheet(state.background);
    QList<QRect> screens;
    if (auto *primary = QGuiApplication::primaryScreen()) screens.push_back(primary->availableGeometry());
    for (auto *screen : QGuiApplication::screens()) screens.push_back(screen->availableGeometry());
    const QRect restored = pin_session::visibleGeometry(state.geometry, screens);
    window->applyingGeometry_ = true;
    window->setGeometry(restored);
    window->applyingGeometry_ = false;
    if (state.thumbnail) {
        if (!window->thumbnail_source_.isValid()) {
            const int side = window->settings_->pinThumbnailSize;
            window->thumbnail_source_ = QRect(QPoint(), state.image.size().boundedTo(QSize(side, side)));
            window->thumbnail_source_.moveCenter(state.image.rect().center());
        }
    } else if (restored.size() != state.geometry.size()) {
        const auto naturalSize = window->pixmap_.deviceIndependentSize();
        window->scale_ = std::min(restored.width() / naturalSize.width(), restored.height() / naturalSize.height());
    }
    window->updateTexture();
    window->setTransparentInput(state.transparentInput);
    return window;
}

std::optional<QPixmap> ImageWindow::render(const std::shared_ptr<QMimeData>& mimedata)
{
    // 1. image
    if (mimedata->hasImage()) {
        return mimedata->imageData().value<QPixmap>();
    }

    // 2. html
    if (mimedata->hasHtml()) {
        QTextEdit view;
        view.setFrameShape(QFrame::NoFrame);
        view.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        view.setLineWrapMode(QTextEdit::NoWrap);

        view.setHtml(mimedata->html());
        view.document()->setDocumentMargin(0);
        view.setFixedSize(view.document()->size().toSize());

        return view.grab();
    }

    // 3. urls
    if (mimedata->hasUrls() && mimedata->urls().size() == 1) {
        const auto path = QFileInfo(mimedata->urls()[0].fileName());
        if (mimedata->urls()[0].isLocalFile() && QFileInfo(mimedata->urls()[0].toLocalFile()).isFile() &&
            QString("jpg;jpeg;png;bmp;ico;svg;webp")
                .split(';')
                .contains(path.suffix(), Qt::CaseInsensitive)) {
            return QPixmap(mimedata->urls()[0].toLocalFile());
        }
    }

    // TODO: text
    if (mimedata->hasUrls() &&
        QString("txt;json;html")
            .split(';')
            .contains(QFileInfo(mimedata->urls()[0].fileName()).suffix(), Qt::CaseInsensitive)) {}

    // TODO: microsoft rich text format
    if (mimedata->hasFormat("text/rtf") || mimedata->hasFormat("text/richtext") ||
        mimedata->hasFormat("application/x-qt-windows-mime;value=\"Rich Text Format\"")) {}

    // 4. text
    if (mimedata->hasText()) {
        auto text = mimedata->text();

        // remove the last '\n'
        if (mimedata->hasUrls() && mimedata->urls().size() > 1 && !text.isEmpty() && text.back() == '\n')
            text = text.mid(0, text.size() - 1);

        QLabel label(text);
        label.setWordWrap(false);
        label.setMargin(10);
        label.setStyleSheet(settings_->definiteTheme() == "light" ? "background-color:white; color: black;"
                                                                : "background-color:black; color: white;");
        return label.grab();
    }

    // 5. color
    if (mimedata->hasColor()) {}

    logw("unsupported");
    return std::nullopt;
}

void ImageWindow::setClosedPinRestoreAvailable(bool available)
{
    if (restore_closed_action_) restore_closed_action_->setEnabled(available);
}

ImageWindow::~ImageWindow()
{
    if (annotation_editor_) disconnect(annotation_editor_, nullptr, this, nullptr);
    delete annotation_editor_;
    annotation_editor_ = nullptr;
}

void ImageWindow::ensureAnnotationEditor()
{
    if (annotation_editor_) return;
    annotation_editor_ = new PinAnnotationEditor(this);
    layout()->addWidget(annotation_editor_);
    annotation_editor_->hide();
    annotation_editor_->document()->load(pixmap_.toImage());
    connect(annotation_editor_, &PinAnnotationEditor::editingFinished, this, [this] { setAnnotationMode(false); });
    connect(annotation_editor_, &PinAnnotationEditor::copyRequested, this, &ImageWindow::copyImage);
    connect(annotation_editor_, &PinAnnotationEditor::saveRequested, this, &ImageWindow::saveAs);
    connect(annotation_editor_, &PinAnnotationEditor::annotationsChanged, this, [this] {
        const auto previousSize = pixmap_.size();
        pixmap_ = QPixmap::fromImage(annotation_editor_->document()->baseImage());
        updateTexture();
        if (previousSize != pixmap_.size()) {
            thumbnail_ = false;
            thumbnail_source_ = {};
            updateGeometryForImage();
        }
    });
    connect(annotation_editor_->document()->undoStack(), &QUndoStack::canUndoChanged, undo_annotation_action_, &QAction::setEnabled);
    connect(annotation_editor_->document()->undoStack(), &QUndoStack::canRedoChanged, redo_annotation_action_, &QAction::setEnabled);
}

QPixmap ImageWindow::compositedPixmap() const
{
    return annotation_editor_ ? QPixmap::fromImage(annotation_editor_->document()->composite()) : pixmap_;
}

void ImageWindow::setAnnotationMode(bool editing)
{
    if (editing == annotation_mode_) return;
    if (editing) {
        if (pixmap_.isNull()) return;
        if (thumbnail_) switchThumbnail(false, rect().center());
        setTransparentInput(false);
        ensureAnnotationEditor();
    } else if (annotation_editor_) annotation_editor_->finishInteraction();
    annotation_mode_ = editing;
    if (annotation_action_) annotation_action_->setChecked(editing);
    texture_->setVisible(!editing);
    if (annotation_editor_) {
        if (!editing) annotation_editor_->setEditing(false);
        annotation_editor_->setVisible(editing);
        if (editing) annotation_editor_->setEditing(true);
    }
    if (!editing) {
        setCursor(Qt::ArrowCursor);
        updateTexture();
    }
}

void ImageWindow::undoAnnotation()
{
    if (!annotation_editor_) return;
    annotation_editor_->finishInteraction();
    annotation_editor_->document()->undoStack()->undo();
}
void ImageWindow::redoAnnotation()
{
    if (!annotation_editor_) return;
    annotation_editor_->finishInteraction();
    annotation_editor_->document()->undoStack()->redo();
}
void ImageWindow::rotateImage(qreal degrees)
{
    setAnnotationMode(false);
    ensureAnnotationEditor();
    annotation_editor_->document()->transformImage(QTransform().rotate(degrees));
}
void ImageWindow::flipImage(bool horizontal)
{
    setAnnotationMode(false);
    ensureAnnotationEditor();
    annotation_editor_->document()->transformImage(QTransform().scale(horizontal ? -1 : 1, horizontal ? 1 : -1));
}
void ImageWindow::grayscaleImage()
{
    setAnnotationMode(false);
    ensureAnnotationEditor();
    annotation_editor_->document()->grayscaleImage();
}
void ImageWindow::moveEvent(QMoveEvent *event)
{
    FramelessWindow::moveEvent(event);
    if (annotation_editor_ && annotation_mode_) annotation_editor_->updateToolbarPosition();
}
void ImageWindow::hideEvent(QHideEvent *event)
{
    if (annotation_editor_) annotation_editor_->setEditing(false);
    FramelessWindow::hideEvent(event);
}
void ImageWindow::showEvent(QShowEvent *event)
{
    FramelessWindow::showEvent(event);
    if (annotation_editor_ && annotation_mode_) annotation_editor_->setEditing(true);
}
