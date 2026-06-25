#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QFileDialog>
#include <QMessageBox>

#include "SDK/header/now2sdk/LFile.h"
#include "SDK/header/now2sdk/LPreview.h"
#include "SDK/header/now2sdk/LOutput.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onSelectFileClicked();
    void onPlayClicked();
    void onPauseClicked();
    void onStopClicked();

    void onDeviceChanged(int index);
    void onChannelChanged(int index);
    void onFormatChanged(int index);
    void onOutputToggled(bool checked);

    void updateUI();
    void updateOutputStats();

private:
    Ui::MainWindow *ui;

    LFile *m_lFile = nullptr;
    LPreview *m_lPreview = nullptr;
    LOutput *m_lOutput = nullptr;

    QTimer *m_uiTimer = nullptr;
    QTimer *m_statsTimer = nullptr;

    bool m_isPlaying = false;
    bool m_isPaused = false;
    
    void refreshDevices();
    void refreshChannels(int deviceIndex);
    void refreshFormats(int deviceIndex, int channelIndex);
    QString msToTimecode(double ms, double fps);
};

#endif 
