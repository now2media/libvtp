#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>

#include "SDK/header/now2sdk/LLive.h"
#include "SDK/header/now2sdk/LPreview.h"
#include "SDK/header/now2sdk/LFilter.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void onLiveClicked();
    void onDeviceChanged(int index);
    void onChannelChanged(int index);
    void onFormatChanged(int index);
    void refreshLiveSources();

private:
    Ui::MainWindow *ui;

    LLive    *m_lLive         = nullptr;
    LPreview *m_lPreviewLive  = nullptr;
    LFilter  *m_colorFilter   = nullptr;
    LFilter  *m_delayFilter   = nullptr;
};

#endif 
