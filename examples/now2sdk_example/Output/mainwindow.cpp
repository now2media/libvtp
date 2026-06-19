#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include <sstream>
#include <iomanip>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_lFile(new LFile()),
      m_lPreview(new LPreview()),
      m_lOutput(new LOutput()),
      m_uiTimer(new QTimer(this)),
      m_statsTimer(new QTimer(this))
{
    ui->setupUi(this);
    setWindowTitle("Broadcast Playout Output Control Dashboard");
    
    // Apply highly premium styling
    setStyleSheet(
        "QMainWindow { background-color: #0f172a; color: #f8fafc; }"
        "QGroupBox { border: 1px solid rgba(255,255,255,0.1); border-radius: 12px; margin-top: 15px; font-weight: bold; padding: 15px; background-color: #1e293b; color: #38bdf8; }"
        "QLabel { color: #cbd5e1; font-weight: 500; }"
        "QComboBox { background-color: #0f172a; border: 1px solid #475569; color: #f8fafc; padding: 8px; border-radius: 6px; min-width: 150px; }"
        "QComboBox:disabled { background-color: #1e293b; color: #64748b; border-color: #334155; }"
        "QPushButton { background-color: #334155; color: #f8fafc; border: none; padding: 10px 20px; border-radius: 6px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #475569; }"
        "QPushButton:disabled { background-color: #1e293b; color: #64748b; }"
        "QCheckBox { color: #f8fafc; spacing: 8px; font-weight: bold; }"
        "QCheckBox::indicator { width: 22px; height: 22px; }"
    );
    
    // Configure File Playout properties
    m_lFile->setProps("eof_hold", "true");
    
    // Enable local preview monitoring
    m_lPreview->setProps("ui_framework", "qt");
    m_lPreview->previewEnable(ui->previewWidget, true, true);
    m_lPreview->previewObject(m_lFile);
    m_lPreview->setProps("audio_meter", "true");
    m_lPreview->setProps("timecode.preview", "true");
    
    // Set output source
    m_lOutput->setSource(m_lFile);
    
    // Button connections
    connect(ui->selectFileBtn, &QPushButton::clicked, this, &MainWindow::onSelectFileClicked);
    connect(ui->playBtn, &QPushButton::clicked, this, &MainWindow::onPlayClicked);
    connect(ui->pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseClicked);
    connect(ui->stopBtn, &QPushButton::clicked, this, &MainWindow::onStopClicked);
    
    // Output combo connections
    connect(ui->deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDeviceChanged);
    connect(ui->channelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onChannelChanged);
    connect(ui->formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFormatChanged);
    
    // Toggle live output send to air
    connect(ui->enableOutputCheck, &QCheckBox::toggled, this, &MainWindow::onOutputToggled);
    
    // Set up UI refresh timers
    connect(m_uiTimer, &QTimer::timeout, this, &MainWindow::updateUI);
    m_uiTimer->start(50);
    
    connect(m_statsTimer, &QTimer::timeout, this, &MainWindow::updateOutputStats);
    m_statsTimer->start(500);
    
    // Load initial devices list
    refreshDevices();
}

MainWindow::~MainWindow() {
    m_uiTimer->stop();
    m_statsTimer->stop();
    
    m_lOutput->setEnabled(false);
    m_lFile->stop();
    
    delete m_lPreview;
    delete m_lOutput;
    delete m_lFile;
    delete ui;
}

void MainWindow::refreshDevices() {
    ui->deviceCombo->blockSignals(true);
    ui->deviceCombo->clear();
    
    int count = 0;
    m_lOutput->DeviceGetCount(count);
    
    for (int i = 0; i < count; i++) {
        std::string name, desc;
        m_lOutput->DeviceGetByIndex(i, name, desc);
        ui->deviceCombo->addItem(QString::fromStdString(name), i);
    }
    
    ui->deviceCombo->blockSignals(false);
    if (count > 0) {
        ui->deviceCombo->setCurrentIndex(0);
        onDeviceChanged(0);
    }
}

void MainWindow::refreshChannels(int deviceIndex) {
    ui->channelCombo->blockSignals(true);
    ui->channelCombo->clear();
    
    int count = 0;
    m_lOutput->DeviceChannelGetCount(deviceIndex, count);
    
    for (int i = 0; i < count; i++) {
        std::string name, desc;
        m_lOutput->DeviceChannelGetByIndex(deviceIndex, i, name, desc);
        ui->channelCombo->addItem(QString::fromStdString(name), i);
    }
    
    ui->channelCombo->blockSignals(false);
    if (count > 0) {
        ui->channelCombo->setCurrentIndex(0);
        onChannelChanged(0);
    }
}

void MainWindow::refreshFormats(int deviceIndex, int channelIndex) {
    ui->formatCombo->blockSignals(true);
    ui->formatCombo->clear();
    
    int count = 0;
    m_lOutput->DeviceFormatVideoGetCount(deviceIndex, channelIndex, count);
    
    for (int i = 0; i < count; i++) {
        std::string name;
        videoFormatProps props;
        m_lOutput->DeviceFormatVideoGetByIndex(deviceIndex, channelIndex, i, name, props);
        ui->formatCombo->addItem(QString::fromStdString(name), i);
    }
    
    ui->formatCombo->blockSignals(false);
    if (count > 0) {
        ui->formatCombo->setCurrentIndex(0);
        onFormatChanged(0);
    }
}

void MainWindow::onDeviceChanged(int index) {
    if (index < 0) return;
    int deviceIndex = ui->deviceCombo->itemData(index).toInt();
    m_lOutput->setDevice(deviceIndex);
    
    refreshChannels(deviceIndex);
}

void MainWindow::onChannelChanged(int index) {
    if (index < 0) return;
    int channelIndex = ui->channelCombo->itemData(index).toInt();
    m_lOutput->DeviceChannelSet(channelIndex);
    
    int deviceIndex = ui->deviceCombo->currentData().toInt();
    refreshFormats(deviceIndex, channelIndex);
}

void MainWindow::onFormatChanged(int index) {
    if (index < 0) return;
    int formatIndex = ui->formatCombo->itemData(index).toInt();
    m_lOutput->DeviceFormatVideoSet(formatIndex);
}

void MainWindow::onOutputToggled(bool checked) {
    if (checked) {
        m_lOutput->setEnabled(true);
        ui->deviceCombo->setEnabled(false);
        ui->channelCombo->setEnabled(false);
        ui->formatCombo->setEnabled(false);
        ui->liveStatusLbl->setText("LIVE OUTPUT TRANSMITTING...");
        ui->liveStatusLbl->setStyleSheet("color: #ef4444; font-weight: bold; font-size: 15px;");
    } else {
        m_lOutput->setEnabled(false);
        ui->deviceCombo->setEnabled(true);
        ui->channelCombo->setEnabled(true);
        ui->formatCombo->setEnabled(true);
        ui->liveStatusLbl->setText("LIVE OUTPUT STANDBY / IDLE");
        ui->liveStatusLbl->setStyleSheet("color: #10b981; font-weight: bold; font-size: 15px;");
    }
}

void MainWindow::onSelectFileClicked() {
    QString filePath = QFileDialog::getOpenFileName(
        this, "Select Playout Media File", "",
        "Media Files (*.mp4 *.mkv *.mov *.avi *.ts *.mxf *.mp3 *.wav *.aac);;All Files (*)"
    );
    if (!filePath.isEmpty()) {
        m_lFile->stop();
        m_lFile->fileNameSet(filePath.toStdString());
        
        QFileInfo fi(filePath);
        ui->fileNameLbl->setText(fi.fileName());
        
        m_isPlaying = false;
        m_isPaused = false;
        ui->playBtn->setEnabled(true);
        ui->pauseBtn->setEnabled(false);
        ui->stopBtn->setEnabled(false);
    }
}

void MainWindow::onPlayClicked() {
    m_lFile->play();
    m_isPlaying = true;
    m_isPaused = false;
    
    ui->playBtn->setEnabled(false);
    ui->pauseBtn->setEnabled(true);
    ui->stopBtn->setEnabled(true);
}

void MainWindow::onPauseClicked() {
    m_lFile->pause();
    m_isPlaying = false;
    m_isPaused = true;
    
    ui->playBtn->setEnabled(true);
    ui->pauseBtn->setEnabled(false);
    ui->stopBtn->setEnabled(true);
}

void MainWindow::onStopClicked() {
    m_lFile->stop();
    m_isPlaying = false;
    m_isPaused = false;
    
    ui->playBtn->setEnabled(true);
    ui->pauseBtn->setEnabled(false);
    ui->stopBtn->setEnabled(false);
}

void MainWindow::updateUI() {
    double curMs = m_lFile->PosGet();
    double durMs = m_lFile->getDurationMs();
    double fps = m_lFile->getFPS();
    if (fps <= 0) fps = 25.0;
    
    if (durMs > 0) {
        ui->progressBar->setMaximum(static_cast<int>(durMs));
        ui->progressBar->setValue(static_cast<int>(curMs));
        ui->timecodeLbl->setText(msToTimecode(curMs, fps) + " / " + msToTimecode(durMs, fps));
    } else {
        ui->progressBar->setMaximum(100);
        ui->progressBar->setValue(0);
        ui->timecodeLbl->setText("00:00:00.00 / 00:00:00.00");
    }
}

void MainWindow::updateOutputStats() {
    LOutput::OutputStats stats;
    m_lOutput->getStats(stats);
    
    ui->statsResolutionVal->setText(QString("%1 x %2").arg(stats.width).arg(stats.height));
    ui->statsFpsVal->setText(QString("%1 FPS").arg(stats.fps, 0, 'f', 2));
    ui->statsAudioVal->setText(QString("%1 Hz, %2 CH").arg(stats.audioSampleRate).arg(stats.audioChannels));
    ui->statsFramesVal->setText(QString::number(stats.displayedFrames));
    ui->statsDroppedVal->setText(QString::number(stats.droppedFrames));
}

QString MainWindow::msToTimecode(double ms, double fps) {
    if (ms < 0) ms = 0;
    if (fps <= 0) fps = 25.0;

    int total_frames = static_cast<int>((ms * fps) / 1000.0);
    int ff = total_frames % static_cast<int>(fps);
    int total_seconds = static_cast<int>(ms / 1000.0);
    int ss = total_seconds % 60;
    int mm = (total_seconds / 60) % 60;
    int hh = total_seconds / 3600;

    std::stringstream ss_tc;
    ss_tc << std::setfill('0') << std::setw(2) << hh << ":"
          << std::setfill('0') << std::setw(2) << mm << ":"
          << std::setfill('0') << std::setw(2) << ss << "."
          << std::setfill('0') << std::setw(2) << ff;
    return QString::fromStdString(ss_tc.str());
}
