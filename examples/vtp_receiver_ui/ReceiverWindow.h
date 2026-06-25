#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <thread>
#include <atomic>
#include <mutex>
#include <vtp.h>
#include "VideoWidget.h"

class ReceiverWindow : public QMainWindow {
    Q_OBJECT

public:
    ReceiverWindow(QWidget* parent = nullptr);
    ~ReceiverWindow();

private slots:
    void onRefreshSources();
    void onToggleConnection();
    void onAutoRefreshTimeout();

private:
    void setupUI();
    void startReceiving();
    void stopReceiving();
    void receiveLoop();

    QComboBox* sourceCombo_;
    QPushButton* refreshBtn_;
    QPushButton* connectBtn_;
    VideoWidget* videoWidget_;
    QLabel* infoLabel_;
    QLabel* statusLabel_;

    class QCheckBox* manualCheck_;
    class QLineEdit* ipEdit_;
    class QSpinBox* portSpin_;
    class QCheckBox* audioCheck_;

    QTimer* discoveryTimer_;

    vtp_listener_t* vtpListener_ = nullptr;
    vtp_receiver_t* vtpReceiver_ = nullptr;

    std::vector<vtp_source_t> discoveredSources_;

    std::thread receiveThread_;
    std::atomic<bool> isReceiving_{false};
    std::atomic<bool> threadRunning_{false};

    std::thread audioReceiveThread_;
    std::atomic<bool> isAudioReceiving_{false};
    void audioReceiveLoop();
    class QAudioSink* audioSink_ = nullptr;
    class QIODevice* audioDevice_ = nullptr;

    std::atomic<uint64_t> lastVideoPts_{0};
    std::atomic<uint64_t> lastAudioPts_{0};
};
