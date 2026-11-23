#ifndef CAPTURER_FRAMELESS_WINDOW_H
#define CAPTURER_FRAMELESS_WINDOW_H

#include <QPointer>
#include <QWidget>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#define Q_NATIVE_EVENT_RESULT qintptr
#else
#define Q_NATIVE_EVENT_RESULT long
#endif

class TitleBar;

class FramelessWindow : public QWidget
{
    Q_OBJECT
public:
    explicit FramelessWindow(QWidget *parent = nullptr, Qt::WindowFlags flags = Qt::WindowFlags());

    [[nodiscard]] bool isSizeFixed() const;

    TitleBar *titlebar();

    virtual void closeSelf() { this->close(); };

public slots:
    void toggleTransparentInput();
    virtual void setTransparentInput(bool enabled);
    bool isTransparentInput() const { return transparent_input_; }

signals:
    void transparentInputChanged(bool enabled);
    void hidden();
    void closed();
    void closeAll();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

    void closeEvent(QCloseEvent *) override;
    void hideEvent(QHideEvent *event) override;

    QPointer<TitleBar> titlebar_{};

    bool transparent_input_{};
    qreal opacity_before_transparent_input_{1.0};

    bool dragmove_{ true };
    int  dragmove_status_{};

#ifdef Q_OS_LINUX
    Qt::Edges edges_{};
    void      updateCursor(Qt::Edges edges);
    bool      event(QEvent *event) override;
#endif

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray& eventType, void *message, Q_NATIVE_EVENT_RESULT *result) override;
#endif
};

#endif //! CAPTURER_FRAMELESS_WINDOW_H
