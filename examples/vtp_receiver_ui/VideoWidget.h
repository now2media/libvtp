#pragma once

#include <QWidget>
#include <QImage>
#include <QMutex>

class VideoWidget : public QWidget {
    Q_OBJECT

public:
    VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    void updateFrame(QImage img);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QImage currentImage_;
    QMutex mutex_;
};
