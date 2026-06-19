#pragma once

#include <QMainWindow>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <vtp.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

class SenderWindow : public QMainWindow {
    Q_OBJECT

public:
    SenderWindow(QWidget* parent = nullptr);
    ~SenderWindow();

private slots:
    void onBrowseFile();
    void onToggleStreaming();

private:
    void setupUI();
    void startStreaming();
    void stopStreaming();
    
    // Dual-thread architecture
    void decodeLoop();
    void videoSendLoop();

    // UI Widgets
    QLineEdit* streamNameEdit_;
    QComboBox* presetCombo_;
    QSpinBox* qualitySpin_;
    QCheckBox* alphaCheck_;
    QCheckBox* audioCheck_;
    QLineEdit* filePathEdit_;
    QPushButton* browseBtn_;
    QPushButton* streamBtn_;
    QLabel* statusLabel_;

    // VTP Sender
    vtp_sender_t* vtpSender_ = nullptr;
    bool isStreaming_ = false;

    // FFmpeg Decoder Thread
    std::thread decodeThread_;
    std::thread videoSendThread_;
    std::atomic<bool> isDecoding_{false};
    std::string selectedFilePath_;

    // Shared Frame Buffer
    std::vector<unsigned char> latestFrameData_;
    std::mutex latestFrameMutex_;
    bool latestFrameNew_ = false;

    // Configured parameters
    int targetWidth_ = 1280;
    int targetHeight_ = 720;
    int targetFps_ = 60;
};
