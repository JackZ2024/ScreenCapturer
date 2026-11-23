#include "TextureWidget.h"

#include <QPainter>

TextureWidget::TextureWidget(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(this, &TextureWidget::arrived, this, [this] { update(); }, Qt::QueuedConnection);
}

void TextureWidget::present(const QPixmap& pixmap)
{
    std::lock_guard lock(mtx_);

    frame_ = pixmap;

    emit arrived();
}

void TextureWidget::present(const QImage& image)
{
    std::lock_guard lock(mtx_);
    frame_ = QPixmap::fromImage(image);

    emit arrived();
}

// TODO: D3D11, OpenGL ...
void TextureWidget::paintEvent(QPaintEvent *)
{
    if (std::lock_guard lock(mtx_); !frame_.isNull()) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const auto imgrect = imageRect(frame_.size(), rect());

        painter.drawPixmap(imgrect, frame_);

        // painter.drawRect(0,0,width()-1, height()-1);
    }
}

QRect TextureWidget::imageRect(const QSize &imageSize, const QRect &area)
{
    QRect result(QPoint(), imageSize.scaled(area.size(), Qt::KeepAspectRatio));
    result.moveCenter(area.center());
    return result;
}
