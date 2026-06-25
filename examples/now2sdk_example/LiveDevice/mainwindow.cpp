#include "mainwindow.h"
#include "./ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      ui(new Ui::MainWindow),
      m_lLive(new LLive()),
      m_lPreviewLive(new LPreview())
{
    ui->setupUi(this);

    m_lLive->setProps("gpu", "true");
    m_lLive->setProps("timecode", "true");
    m_lLive->setProps("timecodeSource", "0");

    m_lPreviewLive->previewEnable(ui->livePreview, true, true);
    m_lPreviewLive->previewObject(m_lLive);
    m_lPreviewLive->setProps("audio_meter", "true");
    m_lPreviewLive->setProps("maintain_ar", "true");
    m_lPreviewLive->setProps("timecode.preview", "true");

    connect(ui->liveBtn, &QPushButton::clicked, this, &MainWindow::onLiveClicked);
    connect(ui->deviceComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onDeviceChanged);
    connect(ui->deviceChannelComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onChannelChanged);
    connect(ui->deviceFormatComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::onFormatChanged);

    refreshLiveSources();
}

MainWindow::~MainWindow() {
    if (m_lLive) m_lLive->Stop();
    delete m_lPreviewLive;
    delete m_lLive;
    delete m_colorFilter;
    delete m_delayFilter;
    delete ui;
}

void MainWindow::refreshLiveSources() {
    ui->deviceComboBox->blockSignals(true);
    ui->deviceComboBox->clear();
    int count = 0;
    m_lLive->DeviceGetCount(count);
    for (int i = 0; i < count; i++) {
        std::string name, desc;
        m_lLive->DeviceGetByIndex(i, name, desc);
        ui->deviceComboBox->addItem(QString::fromStdString(name));
    }
    ui->deviceComboBox->blockSignals(false);
    if (count > 0) {
        ui->deviceComboBox->setCurrentIndex(0);
        onDeviceChanged(0);
    }
}

void MainWindow::onDeviceChanged(int index) {
    if (index < 0) return;

    int status = 0;
    m_lLive->statusGet(status);
    if (status == 1) m_lLive->Stop();

    m_lLive->DeviceSet(index);

    ui->deviceChannelComboBox->blockSignals(true);
    ui->deviceChannelComboBox->clear();
    int count = 0;
    m_lLive->DeviceChannelGetCount(index, count);
    for (int i = 0; i < count; i++) {
        std::string name, desc;
        m_lLive->DeviceChannelGetByIndex(index, i, name, desc);
        ui->deviceChannelComboBox->addItem(QString::fromStdString(name));
    }
    ui->deviceChannelComboBox->blockSignals(false);
    if (count > 0) {
        ui->deviceChannelComboBox->setCurrentIndex(0);
        onChannelChanged(0);
    }

    if (status == 1) m_lLive->Start();
}

void MainWindow::onChannelChanged(int index) {
    if (index < 0) return;
    int deviceIdx = ui->deviceComboBox->currentIndex();

    int status = 0;
    m_lLive->statusGet(status);
    if (status == 1) m_lLive->Stop();

    m_lLive->DeviceChannelSet(index);

    ui->deviceFormatComboBox->blockSignals(true);
    ui->deviceFormatComboBox->clear();
    int count = 0;
    m_lLive->DeviceFormatVideoGetCount(deviceIdx, index, count);
    for (int i = 0; i < count; ++i) {
        std::string name;
        videoFormatProps props;
        m_lLive->DeviceFormatVideoGetByIndex(deviceIdx, index, i, name, props);
        ui->deviceFormatComboBox->addItem(QString::fromStdString(name), i);
    }
    ui->deviceFormatComboBox->blockSignals(false);
    if (count > 0) {
        ui->deviceFormatComboBox->setCurrentIndex(0);
        onFormatChanged(0);
    }

    if (status == 1) m_lLive->Start();
}

void MainWindow::onFormatChanged(int index) {
    if (index < 0) return;
    int status = 0;
    m_lLive->statusGet(status);
    if (status == 1) m_lLive->Stop();
    m_lLive->DeviceFormatVideoSet(index);
    if (status == 1) m_lLive->Start();
}

void MainWindow::onLiveClicked() {
    static bool isLive = false;
    if (!isLive) {
        int formatIdx = ui->deviceFormatComboBox->currentData().toInt();
        m_lLive->DeviceFormatVideoSet(formatIdx);
        if (m_lLive->Start()) {
            isLive = true;
            ui->liveBtn->setText("Stop Live");
            ui->liveBtn->setStyleSheet("background-color: #ff4444; color: white; font-weight: bold;");
        }
    } else {
        m_lLive->Stop();
        isLive = false;
        ui->liveBtn->setText("Start Live");
        ui->liveBtn->setStyleSheet("");
    }
}
