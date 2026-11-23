#include "combobox.h"

#include "platforms/WindowEffect.h"

#include <QListView>
#include <QPlatformSurfaceEvent>

ComboBox::ComboBox(QWidget *parent)
    : QComboBox(parent)
{
    setView(new QListView());
    popup_window_ = view()->window();
    popup_window_->setWindowFlag(Qt::NoDropShadowWindowHint);
    popup_window_->setAttribute(Qt::WA_TranslucentBackground);

#ifdef Q_OS_WIN
    popup_window_->installEventFilter(this);
#endif

    connect(this, &QComboBox::currentIndexChanged, this, [this](int) { emit selected(currentData()); });
}

ComboBox::~ComboBox()
{
    // QComboBox deletes its popup/view in its base destructor. Do not let a
    // surface-destruction event call view() and lazily recreate that hierarchy.
    if (popup_window_) popup_window_->removeEventFilter(this);
    popup_window_.clear();
    disconnect(this, &QComboBox::currentIndexChanged, this, nullptr);
}

#ifdef Q_OS_WIN
bool ComboBox::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::PlatformSurface && popup_window_ && popup_window_.data() == watched) {
        if (dynamic_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() !=
            QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            const auto hwnd = reinterpret_cast<HWND>(popup_window_->winId());
            windows::dwm::set_window_corner(hwnd, DWMWCP_ROUND);
            windows::dwm::blur_behind(hwnd);
            windows::dwm::blur(hwnd, windows::dwm::blur_mode_t::ACRYLIC);
        }
    }
    return QComboBox::eventFilter(watched, event);
}
#endif

ComboBox::ComboBox(const std::vector<std::pair<QVariant, QString>>& items, QWidget *parent)
    : ComboBox(parent)
{
    add(items);
}

ComboBox& ComboBox::add(const std::vector<std::pair<QVariant, QString>>& items)
{
    for (const auto& [value, text] : items) {
        insertItem(count(), text, value);
    }
    return *this;
}

ComboBox& ComboBox::add(const QStringList& texts)
{
    for (const auto& text : texts) {
        auto index = count();
        insertItem(index, text, index);
    }
    return *this;
}

ComboBox& ComboBox::add(const QVariant& value, const QString& text)
{
    insertItem(count(), text, value);
    return *this;
}

ComboBox& ComboBox::select(const QVariant& value)
{
    // trigger if we force the index to be 0 while the current index also is 0.
    auto idx = findData(value);
    if (idx < 0 && currentIndex() == 0) {
        idx = 0;
        emit selected(itemData(0));
    }

    setCurrentIndex(idx);
    return *this;
}

ComboBox& ComboBox::select(const std::string& value)
{
    // trigger if we force the index to be 0 while the current index also is 0.
    auto idx = findData(QString::fromUtf8(value.c_str()));
    if (idx < 0 && currentIndex() == 0) {
        idx = 0;
        emit selected(itemData(0));
    }

    setCurrentIndex(idx);
    return *this;
}
