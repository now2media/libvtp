#ifndef LVIDEOWIDGET_H
#define LVIDEOWIDGET_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QImage>
#include <QMutex>
#include <QTimer>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include "LObject.h"
#include "LSink.h"

extern "C" {
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
}

class LVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions, public LSink {
    Q_OBJECT
public:
    explicit LVideoWidget(QWidget *parent = nullptr);
    ~LVideoWidget();

    void setSource(LObject* source);
    void enableMeter(bool enabled) { meterEnabled = enabled; }
    void setAudioEnabled(bool enabled) { audioEnabled = enabled; }
    void setVideoEnabled(bool enabled) { videoEnabled = enabled; }
    void setMaintainAspectRatio(bool enable) { maintainAspectRatio = enable; }
    void setBackgroundColor(const QColor& color) { bgColor = color; }
    void enableTimecode(bool enable) { m_enableTimecode = enable; }
    void setName(const std::string& name) { m_name = name; update(); }
    void setStatus(const std::string& status) { m_status = status; update(); }
    void enableBorder(bool enable) { borderEnabled = enable; update(); }
    void setBorderColor(const std::string& colorStr);

    void pushVideoFrame(AVFrame* frame) override;
    void pushAudioFrame(AVFrame* frame) override;
    void flush() override;
    void setPaused(bool paused) override;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void renderVUMeter();
    void converterLoop(); // Arka planda renk çevrimi yapan thread
    GLuint compileShaderHelper(GLenum type, const char* source);

    LObject* activeSource = nullptr;
    std::queue<AVFrame*> m_vQueue;
    std::mutex m_qMutex;
    QImage m_frame;
    QMutex m_mutex;
    GLuint m_textureId{0};

    // GPU-accelerated YUV rendering fields
    GLuint m_texY{0};
    GLuint m_texU{0};
    GLuint m_texV{0};
    GLuint m_shaderProgram{0};
    GLint m_texYLocation{-1};
    GLint m_texULocation{-1};
    GLint m_texVLocation{-1};
    struct AVFrame* m_activeFrame{nullptr};
    std::mutex m_frameMutex;

    std::thread workerThread;
    std::atomic<bool> running{false};
    std::atomic<int> m_widgetW{480};
    std::atomic<int> m_widgetH{270};

    bool meterEnabled = true;
    bool audioEnabled = true;
    bool videoEnabled = true;
    bool maintainAspectRatio = true;
    QColor bgColor = Qt::black;
    bool m_enableTimecode = false;
    bool borderEnabled = false;
    QColor m_borderColor{211, 211, 211}; // default light gray
    uint8_t* rgbBuffer = nullptr;
    std::chrono::steady_clock::time_point m_lastPushTime;

    struct PeakRecord {
        double ptsSec;
        float peakL;
        float peakR;
    };
    std::deque<PeakRecord> m_peakHistory;
    std::mutex m_peakHistoryMutex;

    uint32_t m_audioDeviceID{0};
    int m_lastSampleRate{0};
    int m_lastChannels{0};
    int m_lastFormat{-1};
    struct SwrContext* m_swrCtxAudio{nullptr};
    uint8_t* m_audioResampleBuf{nullptr};

    float m_peakL{0}, m_peakR{0};
    float m_peakHoldL{0}, m_peakHoldR{0};
    int m_peakTimerL{0}, m_peakTimerR{0};

    std::string m_name;
    std::string m_status{"empty"};
    std::string m_currentTimecode;
    std::mutex m_tcMutex;
    std::atomic<bool> m_isPaused{false};
};

#endif // LVIDEOWIDGET_H
