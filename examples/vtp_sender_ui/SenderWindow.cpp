#include "SenderWindow.h"
#include <QFileDialog>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <iostream>
#include <vector>
#include <chrono>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
}

SenderWindow::SenderWindow(QWidget* parent) : QMainWindow(parent) {
    setupUI();
}

SenderWindow::~SenderWindow() {
    stopStreaming();
}

void SenderWindow::setupUI() {
    setWindowTitle("VTP Streamer - Sender");
    resize(480, 400);

    // Apply sleek dark premium styling
    setStyleSheet(R"(
        QMainWindow {
            background-color: #121214;
        }
        QLabel {
            color: #E2E2E6;
            font-size: 13px;
            font-weight: bold;
        }
        QLineEdit, QSpinBox, QComboBox {
            background-color: #1C1C1E;
            color: #FFFFFF;
            border: 1px solid #2C2C2E;
            border-radius: 6px;
            padding: 6px;
            font-size: 13px;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border: 1px solid #0A84FF;
        }
        QPushButton {
            background-color: #0A84FF;
            color: #FFFFFF;
            border: none;
            border-radius: 6px;
            padding: 8px 16px;
            font-size: 13px;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #0070E0;
        }
        QPushButton:pressed {
            background-color: #0050B0;
        }
        QCheckBox {
            color: #E2E2E6;
            font-weight: bold;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
        }
    )");

    QWidget* centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    QLabel* headerLabel = new QLabel("VTP Sender Configuration (FFmpeg)", this);
    headerLabel->setStyleSheet("font-size: 18px; color: #0A84FF; margin-bottom: 5px;");
    mainLayout->addWidget(headerLabel);

    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(10);
    formLayout->setLabelAlignment(Qt::AlignRight);

    streamNameEdit_ = new QLineEdit("VTP-Camera-1", this);

    presetCombo_ = new QComboBox(this);
    presetCombo_->addItem("3840x2160 50p (1Gbps)", QVariantList{3840, 2160, 50});
    presetCombo_->addItem("3840x2160 30p (1Gbps)", QVariantList{3840, 2160, 30});
    presetCombo_->addItem("3840x2160 25p (1Gbps)", QVariantList{3840, 2160, 25});
    presetCombo_->addItem("2560x1440 60p (1Gbps)", QVariantList{2560, 1440, 60});
    presetCombo_->addItem("2560x1440 50p (1Gbps)", QVariantList{2560, 1440, 50});
    presetCombo_->addItem("2560x1440 30p (1Gbps)", QVariantList{2560, 1440, 30});
    presetCombo_->addItem("2560x1440 25p (1Gbps)", QVariantList{2560, 1440, 25});
    presetCombo_->addItem("1920x1080 120p (1Gbps)", QVariantList{1920, 1080, 120});
    presetCombo_->addItem("1920x1080 100p (1Gbps)", QVariantList{1920, 1080, 100});
    presetCombo_->addItem("1920x1080 90p (1Gbps)", QVariantList{1920, 1080, 90});
    presetCombo_->addItem("1920x1080 60p (1Gbps / 100Mbps)", QVariantList{1920, 1080, 60});
    presetCombo_->addItem("1920x1080 50p (100Mbps / WiFi)", QVariantList{1920, 1080, 50});
    presetCombo_->addItem("1920x1080 30p (100Mbps / WiFi)", QVariantList{1920, 1080, 30});
    presetCombo_->addItem("1920x1080 25p (100Mbps / WiFi)", QVariantList{1920, 1080, 25});
    presetCombo_->addItem("1280x720 120p (1Gbps / 100Mbps)", QVariantList{1280, 720, 120});
    presetCombo_->addItem("1280x720 100p (100Mbps)", QVariantList{1280, 720, 100});
    presetCombo_->addItem("1280x720 90p (100Mbps)", QVariantList{1280, 720, 90});
    presetCombo_->addItem("1280x720 60p (100Mbps / WiFi)", QVariantList{1280, 720, 60});
    presetCombo_->addItem("1280x720 50p (WiFi)", QVariantList{1280, 720, 50});
    presetCombo_->addItem("1280x720 30p (WiFi)", QVariantList{1280, 720, 30});
    presetCombo_->addItem("1280x720 25p (WiFi)", QVariantList{1280, 720, 25});

    // Default select 1080p50
    presetCombo_->setCurrentIndex(11);

    qualitySpin_ = new QSpinBox(this);
    qualitySpin_->setRange(10, 100);
    qualitySpin_->setValue(90);

    alphaCheck_ = new QCheckBox("Send Alpha (Key/Fill)", this);
    audioCheck_ = new QCheckBox("Send Audio (Original Track)", this);
    audioCheck_->setChecked(true);

    formLayout->addRow("Stream Name:", streamNameEdit_);
    formLayout->addRow("Format Preset:", presetCombo_);
    formLayout->addRow("Scale Quality (10-100):", qualitySpin_);
    formLayout->addRow("", alphaCheck_);
    formLayout->addRow("", audioCheck_);

    // Real-time quality change connection
    connect(qualitySpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (vtpSender_) {
            vtp_sender_set_scale_quality(vtpSender_, val);
        }
    });

    mainLayout->addLayout(formLayout);

    QHBoxLayout* fileLayout = new QHBoxLayout();
    filePathEdit_ = new QLineEdit(this);
    filePathEdit_->setPlaceholderText("Select a video file to stream...");
    filePathEdit_->setReadOnly(true);
    browseBtn_ = new QPushButton("Browse", this);
    connect(browseBtn_, &QPushButton::clicked, this, &SenderWindow::onBrowseFile);
    fileLayout->addWidget(filePathEdit_);
    fileLayout->addWidget(browseBtn_);
    mainLayout->addLayout(fileLayout);

    streamBtn_ = new QPushButton("Start Streaming", this);
    streamBtn_->setStyleSheet(R"(
        QPushButton {
            background-color: #30D158;
            color: #FFFFFF;
            font-size: 14px;
            padding: 12px;
        }
        QPushButton:hover {
            background-color: #24B148;
        }
    )");
    connect(streamBtn_, &QPushButton::clicked, this, &SenderWindow::onToggleStreaming);
    mainLayout->addWidget(streamBtn_);

    statusLabel_ = new QLabel("Status: Idle", this);
    statusLabel_->setStyleSheet("color: #8E8E93; font-style: italic;");
    mainLayout->addWidget(statusLabel_);

    setCentralWidget(centralWidget);
}

void SenderWindow::onBrowseFile() {
    QString fileName = QFileDialog::getOpenFileName(
        this, "Open Video File", "", "Video Files (*.mp4 *.mkv *.avi *.mov *.flv *.ts);;All Files (*)");
    if (!fileName.isEmpty()) {
        filePathEdit_->setText(fileName);
        selectedFilePath_ = fileName.toStdString();
    }
}

void SenderWindow::onToggleStreaming() {
    if (isStreaming_) {
        stopStreaming();
    } else {
        startStreaming();
    }
}

void SenderWindow::startStreaming() {
    QString streamName = streamNameEdit_->text().trimmed();
    if (streamName.isEmpty()) {
        QMessageBox::warning(this, "Configuration Error", "Please specify a Stream Name.");
        return;
    }
    if (selectedFilePath_.empty()) {
        QMessageBox::warning(this, "Configuration Error", "Please select a video file first.");
        return;
    }

    QVariantList presetData = presetCombo_->currentData().toList();
    targetWidth_ = presetData.at(0).toInt();
    targetHeight_ = presetData.at(1).toInt();
    targetFps_ = presetData.at(2).toInt();
    bool enableAlpha = alphaCheck_->isChecked();
    bool enableAudio = audioCheck_->isChecked();

    vtpSender_ = vtp_create_sender(streamName.toUtf8().constData(), targetWidth_, targetHeight_, targetFps_, enableAlpha, enableAudio);
    if (!vtpSender_ || !vtp_sender_start(vtpSender_)) {
        QMessageBox::critical(this, "VTP Error", "Failed to create/start VTP Sender.");
        if (vtpSender_) {
            vtp_destroy_sender(vtpSender_);
            vtpSender_ = nullptr;
        }
        return;
    }

    // Set initial scale quality
    vtp_sender_set_scale_quality(vtpSender_, qualitySpin_->value());

    latestFrameData_.assign(targetWidth_ * targetHeight_ * 4, 0);
    latestFrameNew_ = false;

    isStreaming_ = true;
    isDecoding_ = true;
    
    // Spawn threads
    decodeThread_ = std::thread(&SenderWindow::decodeLoop, this);
    videoSendThread_ = std::thread(&SenderWindow::videoSendLoop, this);

    streamNameEdit_->setEnabled(false);
    presetCombo_->setEnabled(false);
    alphaCheck_->setEnabled(false);
    audioCheck_->setEnabled(false);
    browseBtn_->setEnabled(false);

    streamBtn_->setText("Stop Streaming");
    streamBtn_->setStyleSheet(R"(
        QPushButton {
            background-color: #FF453A;
            color: #FFFFFF;
            font-size: 14px;
            padding: 12px;
        }
        QPushButton:hover {
            background-color: #D63025;
        }
    )");

    statusLabel_->setText(QString("Status: Streaming active on port %1").arg(vtp_sender_get_port(vtpSender_)));
}

void SenderWindow::stopStreaming() {
    if (!isStreaming_) return;

    isDecoding_ = false;
    isStreaming_ = false;

    if (decodeThread_.joinable()) {
        decodeThread_.join();
    }
    if (videoSendThread_.joinable()) {
        videoSendThread_.join();
    }

    if (vtpSender_) {
        vtp_sender_stop(vtpSender_);
        vtp_destroy_sender(vtpSender_);
        vtpSender_ = nullptr;
    }

    streamNameEdit_->setEnabled(true);
    presetCombo_->setEnabled(true);
    alphaCheck_->setEnabled(true);
    audioCheck_->setEnabled(true);
    browseBtn_->setEnabled(true);

    streamBtn_->setText("Start Streaming");
    streamBtn_->setStyleSheet(R"(
        QPushButton {
            background-color: #30D158;
            color: #FFFFFF;
            font-size: 14px;
            padding: 12px;
        }
        QPushButton:hover {
            background-color: #24B148;
        }
    )");

    statusLabel_->setText("Status: Idle");
}

void SenderWindow::videoSendLoop() {
    auto interval = std::chrono::microseconds(1000000 / targetFps_);
    auto nextSendTime = std::chrono::steady_clock::now();
    std::vector<unsigned char> sendBuffer(targetWidth_ * targetHeight_ * 4);

    while (isStreaming_) {
        {
            std::lock_guard<std::mutex> lock(latestFrameMutex_);
            std::copy(latestFrameData_.begin(), latestFrameData_.end(), sendBuffer.begin());
        }

        uint64_t now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        if (vtpSender_) {
            vtp_sender_send_frame(vtpSender_, sendBuffer.data(), VTP_FORMAT_RGBA, now_ns);
        }

        nextSendTime += interval;
        std::this_thread::sleep_until(nextSendTime);
    }
}

void SenderWindow::decodeLoop() {
    AVFormatContext* formatContext = nullptr;
    if (avformat_open_input(&formatContext, selectedFilePath_.c_str(), nullptr, nullptr) < 0) {
        std::cerr << "[FFmpeg] avformat_open_input failed" << std::endl;
        return;
    }

    if (avformat_find_stream_info(formatContext, nullptr) < 0) {
        std::cerr << "[FFmpeg] avformat_find_stream_info failed" << std::endl;
        avformat_close_input(&formatContext);
        return;
    }

    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    for (unsigned int i = 0; i < formatContext->nb_streams; ++i) {
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex < 0) {
            videoStreamIndex = i;
        }
        if (formatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex < 0) {
            audioStreamIndex = i;
        }
    }

    if (videoStreamIndex < 0) {
        std::cerr << "[FFmpeg] No video stream found" << std::endl;
        avformat_close_input(&formatContext);
        return;
    }

    // Video Decoder
    AVCodecParameters* videoCodecParams = formatContext->streams[videoStreamIndex]->codecpar;
    const AVCodec* videoCodec = avcodec_find_decoder(videoCodecParams->codec_id);
    AVCodecContext* videoCodecContext = avcodec_alloc_context3(videoCodec);
    avcodec_parameters_to_context(videoCodecContext, videoCodecParams);
    if (avcodec_open2(videoCodecContext, videoCodec, nullptr) < 0) {
        std::cerr << "[FFmpeg] Failed to open video decoder" << std::endl;
        avcodec_free_context(&videoCodecContext);
        avformat_close_input(&formatContext);
        return;
    }

    // Audio Decoder
    AVCodecContext* audioCodecContext = nullptr;
    if (audioStreamIndex >= 0 && audioCheck_->isChecked()) {
        AVCodecParameters* audioCodecParams = formatContext->streams[audioStreamIndex]->codecpar;
        const AVCodec* audioCodec = avcodec_find_decoder(audioCodecParams->codec_id);
        if (audioCodec) {
            audioCodecContext = avcodec_alloc_context3(audioCodec);
            avcodec_parameters_to_context(audioCodecContext, audioCodecParams);
            if (avcodec_open2(audioCodecContext, audioCodec, nullptr) < 0) {
                avcodec_free_context(&audioCodecContext);
                audioCodecContext = nullptr;
            }
        }
    }

    // Video SwsContext (Scale to target resolution and convert to RGBA)
    SwsContext* swsContext = sws_getContext(
        videoCodecContext->width, videoCodecContext->height, videoCodecContext->pix_fmt,
        targetWidth_, targetHeight_, AV_PIX_FMT_RGBA,
        SWS_BICUBIC, nullptr, nullptr, nullptr
    );

    AVFrame* rgbaFrame = av_frame_alloc();
    rgbaFrame->width = targetWidth_;
    rgbaFrame->height = targetHeight_;
    rgbaFrame->format = AV_PIX_FMT_RGBA;
    av_frame_get_buffer(rgbaFrame, 0);

    // Audio SwrContext (Resample to 48000Hz Stereo 16-bit PCM)
    SwrContext* swrContext = nullptr;
    if (audioCodecContext) {
        swrContext = swr_alloc();
        AVChannelLayout in_ch_layout = audioCodecContext->ch_layout;
        AVChannelLayout out_ch_layout;
        av_channel_layout_default(&out_ch_layout, 2);

        swr_alloc_set_opts2(
            &swrContext,
            &out_ch_layout, AV_SAMPLE_FMT_S16, 48000,
            &in_ch_layout, audioCodecContext->sample_fmt, audioCodecContext->sample_rate,
            0, nullptr
        );
        swr_init(swrContext);
    }

    AVPacket* pkt = av_packet_alloc();
    AVFrame* decFrame = av_frame_alloc();

    auto startTime = std::chrono::steady_clock::now();
    uint64_t start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        startTime.time_since_epoch()).count();
    
    double first_pts_sec = -1.0;

    while (isDecoding_) {
        int read_res = av_read_frame(formatContext, pkt);
        if (read_res < 0) {
            // Loop playback
            av_seek_frame(formatContext, -1, 0, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(videoCodecContext);
            if (audioCodecContext) {
                avcodec_flush_buffers(audioCodecContext);
            }
            startTime = std::chrono::steady_clock::now();
            start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                startTime.time_since_epoch()).count();
            first_pts_sec = -1.0;
            continue;
        }

        double timeBase = av_q2d(formatContext->streams[pkt->stream_index]->time_base);
        int64_t pts = (pkt->pts != AV_NOPTS_VALUE) ? pkt->pts : pkt->dts;

        if (pkt->stream_index == videoStreamIndex) {
            int send_res = avcodec_send_packet(videoCodecContext, pkt);
            while (send_res >= 0) {
                int rec_res = avcodec_receive_frame(videoCodecContext, decFrame);
                if (rec_res == AVERROR(EAGAIN) || rec_res == AVERROR_EOF) {
                    break;
                } else if (rec_res < 0) {
                    break;
                }

                // Scale & convert to RGBA
                sws_scale(
                    swsContext, decFrame->data, decFrame->linesize, 0, videoCodecContext->height,
                    rgbaFrame->data, rgbaFrame->linesize
                );

                double frame_pts_val = (decFrame->pts != AV_NOPTS_VALUE ? decFrame->pts : 0);
                double frame_pts_sec = frame_pts_val * timeBase;
                if (first_pts_sec < 0.0) {
                    first_pts_sec = frame_pts_sec;
                }
                double relative_frame_pts = frame_pts_sec - first_pts_sec;

                // Sync video frame playback speed
                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - startTime).count() / 1000000.0;
                if (relative_frame_pts > elapsed) {
                    double delay = relative_frame_pts - elapsed;
                    if (delay < 1.0) {
                        std::this_thread::sleep_for(std::chrono::duration<double>(delay));
                    }
                }

                {
                    std::lock_guard<std::mutex> lock(latestFrameMutex_);
                    std::copy(rgbaFrame->data[0], rgbaFrame->data[0] + (targetWidth_ * targetHeight_ * 4), latestFrameData_.begin());
                    latestFrameNew_ = true;
                }
            }
        } else if (pkt->stream_index == audioStreamIndex && audioCodecContext) {

            int send_res = avcodec_send_packet(audioCodecContext, pkt);
            while (send_res >= 0) {
                int rec_res = avcodec_receive_frame(audioCodecContext, decFrame);
                if (rec_res == AVERROR(EAGAIN) || rec_res == AVERROR_EOF) {
                    break;
                } else if (rec_res < 0) {
                    break;
                }

                int max_out_samples = av_rescale_rnd(
                    swr_get_delay(swrContext, audioCodecContext->sample_rate) + decFrame->nb_samples,
                    48000, audioCodecContext->sample_rate, AV_ROUND_UP
                );

                std::vector<uint8_t> pcmBuffer(max_out_samples * 2 * 2);
                uint8_t* out_data[1] = { pcmBuffer.data() };

                int out_samples = swr_convert(
                    swrContext, out_data, max_out_samples,
                    (const uint8_t**)decFrame->data, decFrame->nb_samples
                );

                if (out_samples > 0) {
                    double audio_pts_val = (decFrame->pts != AV_NOPTS_VALUE ? decFrame->pts : 0);
                    double audio_pts_sec = audio_pts_val * timeBase;
                    if (first_pts_sec < 0.0) {
                        first_pts_sec = audio_pts_sec;
                    }
                    double relative_audio_pts = audio_pts_sec - first_pts_sec;
                    uint64_t pts_ns = start_ns + static_cast<uint64_t>(relative_audio_pts * 1e9);

                    if (vtpSender_) {
                        vtp_sender_send_audio(vtpSender_, pcmBuffer.data(), out_samples, 48000, 2, pts_ns);
                    }
                }
            }
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&decFrame);
    av_frame_free(&rgbaFrame);
    sws_freeContext(swsContext);

    if (swrContext) {
        swr_free(&swrContext);
    }

    avcodec_free_context(&videoCodecContext);
    if (audioCodecContext) {
        avcodec_free_context(&audioCodecContext);
    }

    avformat_close_input(&formatContext);
}
