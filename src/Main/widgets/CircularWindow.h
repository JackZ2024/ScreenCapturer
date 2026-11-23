#ifndef CAPTURER_CIRCULARWINDOW_H
#define CAPTURER_CIRCULARWINDOW_H

#include <QWidget>
#include <QImage>


class CircularWindow : public QWidget
{
    Q_OBJECT

public:
    explicit CircularWindow(QWidget *parent = nullptr);
    ~CircularWindow();

    enum RecordState{
        None,
        Start,
        Pause,
        Continue
    };

    void startRecord();
    void pauseRecord();
    void continueRecord();

signals:
    void canStart();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void updateTime();

private:
    QTimer *timer_;
    QImage background_;

    RecordState curState_ = RecordState::None;
    int curTime_ = 3;
};

#endif //! CAPTURER_CIRCULARWINDOW_H
