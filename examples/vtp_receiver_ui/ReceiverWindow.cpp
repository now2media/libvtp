#include "ReceiverWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QCoreApplication>
#include <QAudioSink>
#include <QAudioFormat>
#include <QIODevice>
#include <QCheckBox>
#include <QLineEdit>
#include <QSpinBox>
#include <iostream>

ReceiverWindow::ReceiverWindow(QWidget* parent) : QMainWindow(parent) {
    setupUI();

    vtpListener_ = vtp_create_listener();
    vtp_listener_start(vtpListener_);

    vtpReceiver_ = vtp_create_receiver();

    discoveryTimer_ = new QTimer(this);
    connect(discoveryTimer_, &QTimer::timeout, this, &ReceiverWindow::onAutoRefreshTimeout);
    discoveryTimer_->start(1000); 
}

ReceiverWindow::~ReceiverWindow() {
    stopReceiving();

    if (vtpReceiver_) {
        vtp_destroy_receiver(vtpReceiver_);
    }

    if (vtpListener_) {
        vtp_listener_stop(vtpListener_);
        vtp_destroy_listener(vtpListener_);
    }
}

void ReceiverWindow::setupUI() {
    setWindowTitle("VTP Receiver - Player");
    resize(800, 600);

    setStyleSheet(R"(
        QMainWindow {
            background-color: #121214;
        }
        QLabel {
            color: #E2E2E6;
            font-size: 13px;
        }
        QComboBox {
            background-color: #1C1C1E;
            color: #FFFFFF;
            border: 1px solid #2C2C2E;
            border-radius: 6px;
            padding: 6px;
            min-width: 200px;
        }
        QComboBox::drop-down {
            border: none;
        }
        QPushButton {
            background-color: #1C1C1E;
            color: #E2E2E6;
            border: 1px solid #2C2C2E;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #2C2C2E;
        }
        QPushButton#connectBtn {
            background-color: #30D158;
            color: #FFFFFF;
            border: none;
        }
        QPushButton#connectBtn:hover {
            background-color: #24B148;
        }
    )");

    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(10);

    QHBoxLayout* controlsLayout = new QHBoxLayout();
    controlsLayout->setSpacing(10);

    QLabel* label = new QLabel("Active VTP Streams:", this);
    label->setStyleSheet("font-weight: bold; color: #0A84FF;");
    controlsLayout->addWidget(label);

    sourceCombo_ = new QComboBox(this);
    controlsLayout->addWidget(sourceCombo_);

    refreshBtn_ = new QPushButton("Refresh", this);
    connect(refreshBtn_, &QPushButton::clicked, this, &ReceiverWindow::onRefreshSources);
    controlsLayout->addWidget(refreshBtn_);

    connectBtn_ = new QPushButton("Start Playing", this);
    connectBtn_->setObjectName("connectBtn");
    connect(connectBtn_, &QPushButton::clicked, this, &ReceiverWindow::onToggleConnection);
    controlsLayout->addWidget(connectBtn_);

    controlsLayout->addStretch();
    mainLayout->addLayout(controlsLayout);

    QHBoxLayout* manualLayout = new QHBoxLayout();
    manualLayout->setSpacing(10);

    manualCheck_ = new QCheckBox("Manual Connect", this);
    manualLayout->addWidget(manualCheck_);

    QLabel* ipLabel = new QLabel("IP:", this);
    manualLayout->addWidget(ipLabel);

    ipEdit_ = new QLineEdit("192.168.1.100", this);
    ipEdit_->setEnabled(false);
    manualLayout->addWidget(ipEdit_);

    QLabel* portLabel = new QLabel("Port:", this);
    manualLayout->addWidget(portLabel);

    portSpin_ = new QSpinBox(this);
    portSpin_->setRange(1024, 65535);
    portSpin_->setValue(5000);
    portSpin_->setEnabled(false);
    manualLayout->addWidget(portSpin_);

    audioCheck_ = new QCheckBox("Has Audio", this);
    audioCheck_->setChecked(true);
    audioCheck_->setEnabled(false);
    manualLayout->addWidget(audioCheck_);

    manualLayout->addStretch();
    mainLayout->addLayout(manualLayout);

    connect(manualCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        ipEdit_->setEnabled(checked);
        portSpin_->setEnabled(checked);
        audioCheck_->setEnabled(checked);
        sourceCombo_->setEnabled(!checked);
        refreshBtn_->setEnabled(!checked);
    });

    videoWidget_ = new VideoWidget(this);
    videoWidget_->setMinimumSize(480, 270);
    mainLayout->addWidget(videoWidget_, 1); 

    QHBoxLayout* infoLayout = new QHBoxLayout();
    infoLabel_ = new QLabel("Stream Info: -", this);
    infoLabel_->setStyleSheet("color: #E2E2E6; font-size: 13px; font-weight: bold;");
    infoLayout->addWidget(infoLabel_);
    
    infoLayout->addStretch();

    statusLabel_ = new QLabel("Disconnected", this);
    statusLabel_->setStyleSheet("color: #FF453A; font-weight: bold;");
    infoLayout->addWidget(statusLabel_);

    mainLayout->addLayout(infoLayout);

    setCentralWidget(centralWidget);
}

void ReceiverWindow::onRefreshSources() {
    
    onAutoRefreshTimeout();
}

void ReceiverWindow::onAutoRefreshTimeout() {
    QString currentSelect = sourceCombo_->currentText();
    sourceCombo_->clear();

    vtp_source_t sources[VTP_MAX_SOURCES];
    int count = vtp_listener_get_sources(vtpListener_, sources, VTP_MAX_SOURCES);
    discoveredSources_.clear();

    for (int i = 0; i < count; ++i) {
        discoveredSources_.push_back(sources[i]);
        QString text = QString("%1").arg(sources[i].name);
        sourceCombo_->addItem(text);
    }

    int idx = sourceCombo_->findText(currentSelect);
    if (idx != -1) {
        sourceCombo_->setCurrentIndex(idx);
    }
}

void ReceiverWindow::onToggleConnection() {
    if (isReceiving_) {
        stopReceiving();
    } else {
        startReceiving();
    }
}

void ReceiverWindow::startReceiving() {
    bool hasAudio = false;

    if (manualCheck_->isChecked()) {
        QString ip = ipEdit_->text().trimmed();
        uint16_t port = portSpin_->value();
        hasAudio = audioCheck_->isChecked();

        if (ip.isEmpty()) {
            QMessageBox::warning(this, "Connection Error", "Please enter a valid IP address.");
            return;
        }

        if (!vtp_receiver_connect(vtpReceiver_, ip.toUtf8().constData(), port)) {
            QMessageBox::critical(this, "Connection Error", QString("Failed to connect to VTP Sender at %1:%2.").arg(ip).arg(port));
            return;
        }
    } else {
        QString streamName = sourceCombo_->currentText();
        if (streamName.isEmpty()) {
            QMessageBox::warning(this, "Connection Error", "Please select a VTP stream first.");
            return;
        }

        if (!vtp_receiver_connect_by_name(vtpReceiver_, vtpListener_, streamName.toUtf8().constData())) {
            QMessageBox::critical(this, "Connection Error", "Failed to connect to VTP stream by name.");
            return;
        }

        for (const auto& src : discoveredSources_) {
            if (QString(src.name) == streamName) {
                hasAudio = src.has_audio;
                break;
            }
        }
    }

    lastVideoPts_ = 0;
    lastAudioPts_ = 0;

    if (hasAudio) {
        QAudioFormat format;
        format.setSampleRate(48000);
        format.setChannelCount(2);
        format.setSampleFormat(QAudioFormat::Int16);

        audioSink_ = new QAudioSink(format, this);
        audioDevice_ = audioSink_->start();

        isAudioReceiving_ = true;
        audioReceiveThread_ = std::thread(&ReceiverWindow::audioReceiveLoop, this);
    }

    isReceiving_ = true;
    threadRunning_ = true;
    receiveThread_ = std::thread(&ReceiverWindow::receiveLoop, this);

    manualCheck_->setEnabled(false);
    ipEdit_->setEnabled(false);
    portSpin_->setEnabled(false);
    audioCheck_->setEnabled(false);
    sourceCombo_->setEnabled(false);
    refreshBtn_->setEnabled(false);
    discoveryTimer_->stop();

    connectBtn_->setText("Stop Playing");
    connectBtn_->setStyleSheet(R"(
        QPushButton#connectBtn {
            background-color: #FF453A;
            color: #FFFFFF;
            border: none;
        }
        QPushButton#connectBtn:hover {
            background-color: #D63025;
        }
    )");

    statusLabel_->setText("Connected");
    statusLabel_->setStyleSheet("color: #30D158;");
}

void ReceiverWindow::stopReceiving() {
    if (!isReceiving_) return;

    isReceiving_ = false;

    if (vtpReceiver_) {
        vtp_receiver_disconnect(vtpReceiver_);
    }

    isAudioReceiving_ = false;
    if (audioReceiveThread_.joinable()) {
        audioReceiveThread_.join();
    }

    if (audioSink_) {
        audioSink_->stop();
        delete audioSink_;
        audioSink_ = nullptr;
        audioDevice_ = nullptr;
    }

    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
    threadRunning_ = false;

    videoWidget_->clear();
    infoLabel_->setText("Stream Info: -");

    manualCheck_->setEnabled(true);
    if (manualCheck_->isChecked()) {
        ipEdit_->setEnabled(true);
        portSpin_->setEnabled(true);
        audioCheck_->setEnabled(true);
    } else {
        sourceCombo_->setEnabled(true);
        refreshBtn_->setEnabled(true);
    }
    discoveryTimer_->start(1000);

    connectBtn_->setText("Start Playing");
    connectBtn_->setStyleSheet(R"(
        QPushButton#connectBtn {
            background-color: #30D158;
            color: #FFFFFF;
            border: none;
        }
        QPushButton#connectBtn:hover {
            background-color: #24B148;
        }
    )");

    statusLabel_->setText("Disconnected");
    statusLabel_->setStyleSheet("color: #FF453A;");
}

void ReceiverWindow::receiveLoop() {
    vtp_frame_t frame;

    int frameCount = 0;
    auto lastFpsTime = std::chrono::steady_clock::now();
    double currentFps = 0.0;

    while (isReceiving_ && vtp_receiver_is_connected(vtpReceiver_)) {
        bool success = vtp_receiver_receive_frame(vtpReceiver_, &frame);

        if (success) {
            uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();

            double latency_ms = (now_ns - frame.timestamp_ns) / 1000000.0;

            lastVideoPts_ = frame.timestamp_ns;

            double drift_ms = 0.0;
            uint64_t last_a = lastAudioPts_.load();
            uint64_t last_v = lastVideoPts_.load();
            if (last_a > 0 && last_v > 0) {
                drift_ms = (static_cast<int64_t>(last_v) - static_cast<int64_t>(last_a)) / 1000000.0;
            }

            QImage::Format qFormat = QImage::Format_RGB888;
            if (frame.format == VTP_FORMAT_RGBA) qFormat = QImage::Format_RGBA8888;
            else if (frame.format == VTP_FORMAT_BGR) qFormat = QImage::Format_BGR888;
            else if (frame.format == VTP_FORMAT_BGRA) qFormat = QImage::Format_ARGB32;

            QImage image(frame.data, frame.width, frame.height, qFormat);

            frameCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFpsTime).count();
            if (elapsed >= 1000) {
                currentFps = (frameCount * 1000.0) / elapsed;
                frameCount = 0;
                lastFpsTime = now;
            }

            QMetaObject::invokeMethod(this, [this, img = image.copy(), frame, latency_ms, currentFps, drift_ms]() {
                videoWidget_->updateFrame(img);
                
                QString driftStr = "-";
                if (lastAudioPts_ > 0) {
                    driftStr = QString("%1 ms").arg(QString::number(drift_ms, 'f', 1));
                }

                infoLabel_->setText(QString("Format: %1x%2 | FPS: %3 | Latency: %4 ms | Drift: %5 | Alpha: %6")
                    .arg(frame.width)
                    .arg(frame.height)
                    .arg(QString::number(currentFps, 'f', 1))
                    .arg(QString::number(latency_ms, 'f', 1))
                    .arg(driftStr)
                    .arg(frame.has_alpha ? "Yes" : "No"));
            });
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    if (isReceiving_) {
        QMetaObject::invokeMethod(this, [this]() {
            stopReceiving();
        });
    }
}

void ReceiverWindow::audioReceiveLoop() {
    vtp_audio_frame_t audioFrame;
    while (isAudioReceiving_ && vtp_receiver_is_connected(vtpReceiver_)) {
        if (vtp_receiver_receive_audio(vtpReceiver_, &audioFrame)) {
            
            lastAudioPts_ = audioFrame.timestamp_ns;

            if (audioDevice_) {
                audioDevice_->write(reinterpret_cast<const char*>(audioFrame.data), audioFrame.data_size);
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
