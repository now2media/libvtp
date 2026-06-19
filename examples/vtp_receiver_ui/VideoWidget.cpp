#include "VideoWidget.h"
#include <QPainter>

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent);
}

VideoWidget::~VideoWidget() {}

void VideoWidget::updateFrame(QImage img) {
    QMutexLocker locker(&mutex_);
    currentImage_ = std::move(img);
    locker.unlock();
    update(); // Trigger repaint
}

void VideoWidget::clear() {
    QMutexLocker locker(&mutex_);
    currentImage_ = QImage();
    locker.unlock();
    update();
}

void VideoWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    QMutexLocker locker(&mutex_);
    if (currentImage_.isNull()) {
        // Draw elegant standby background (dark gray grid placeholder)
        painter.fillRect(rect(), QColor("#1C1C1E"));
        
        painter.setPen(QColor("#8E8E93"));
        painter.setFont(QFont("Arial", 14, QFont::Bold));
        painter.drawText(rect(), Qt::AlignCenter, "No Active VTP Stream\nSelect a source and click Start");
    } else {
        // Clear background with black
        painter.fillRect(rect(), Qt::black);
        
        // Scale and center image keeping aspect ratio on-the-fly without calling .scaled()
        double imageRatio = (double)currentImage_.width() / currentImage_.height();
        double widgetRatio = (double)width() / height();
        
        int targetW = width();
        int targetH = height();
        if (widgetRatio > imageRatio) {
            targetW = height() * imageRatio;
        } else {
            targetH = width() / imageRatio;
        }
        
        int x = (width() - targetW) / 2;
        int y = (height() - targetH) / 2;
        
        QRect targetRect(x, y, targetW, targetH);
        painter.drawImage(targetRect, currentImage_);
    }
}
