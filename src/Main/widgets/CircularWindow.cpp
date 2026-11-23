#include "CircularWindow.h"

#include <QPainter>
#include <QTimer>

CircularWindow::CircularWindow(QWidget *parent) : QWidget(parent)
{
    // 设置窗口无边框和透明背景
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);

    this->resize(200, 200);
    background_.load(":/icons/CircleBackground");

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &CircularWindow::updateTime);
}

CircularWindow::~CircularWindow()
{
    // qDebug() << "---------CircularWindow---------------";
}

void CircularWindow::startRecord()
{
    curState_ = RecordState::Start;
    curTime_ = 3;
    this->show();
    timer_->start(1000);
}

void CircularWindow::pauseRecord()
{
    curState_ = RecordState::Pause;
    curTime_ = 2;
    this->show();
    // this->repaint();
    timer_->start(600);
}

void CircularWindow::continueRecord()
{
    curState_ = RecordState::Continue;
    curTime_ = 2;
    this->show();
    // this->repaint();
    timer_->start(600);
}

void CircularWindow::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 绘制圆形窗口
    painter.drawImage(0, 0, background_);
    painter.save();
    QPen pen = painter.pen();
    pen.setColor(QColor(30, 30, 30));
    painter.setPen(pen);

    if(curState_ == RecordState::Start) {
        QFont font("Arial");
        font.setBold(true);
        font.setPixelSize(130);
        painter.setFont(font);
        painter.drawText(0, 0, 200, 200, Qt::AlignCenter, QString::number(curTime_));
    }
    else if(curState_ == RecordState::Pause) {
        QFont font("宋体");
        font.setBold(true);
        font.setPixelSize(70);
        painter.setFont(font);
        painter.drawText(0, 0, 200, 200, Qt::AlignCenter, "暂停");
    }
    else if(curState_ == RecordState::Continue) {
        QFont font("宋体");
        font.setBold(true);
        font.setPixelSize(70);
        painter.setFont(font);
        painter.drawText(0, 0, 200, 200, Qt::AlignCenter, "继续");
    }
    painter.restore();
    // 设置窗口形状为圆形
    // QBitmap bitmap(size());
    // bitmap.fill();
    // QPainter painterBitmap(&bitmap);
    // painterBitmap.setBrush(QBrush(Qt::black));
    // painterBitmap.drawEllipse(0, 0, width(), height());
    // setMask(bitmap);
}

void CircularWindow::updateTime()
{
    if(curTime_ == 1) {
        this->hide();
        curTime_--;
    }
    else if(curTime_ == 0) {
        timer_->stop();
        if(curState_ == RecordState::Start || curState_ == RecordState::Continue) {
            emit canStart();
        }
    }
    else {
        curTime_--;
        this->update();
    }
}
