#ifndef LVIDEOWIDGETGTK_H
#define LVIDEOWIDGETGTK_H

#include "LSink.h"
#include <gtk/gtk.h>
#include <thread>
#include <mutex>
#include <queue>
#include <string>
#include <SDL2/SDL.h>
#include <atomic>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class LObject;

class LVideoWidgetGTK : public LSink {
public:
    LVideoWidgetGTK();
    ~LVideoWidgetGTK();

    void setSource(LObject* source);
    void pushVideoFrame(AVFrame* frame) override;
    void pushAudioFrame(AVFrame* frame) override;
    void flush() override;
    void setPaused(bool paused) override;

    GtkWidget* getWidget() { return m_mainWidget; }
    
    void setStatus(const std::string& status);
    void setName(const std::string& name);
    void setMaintainAspectRatio(bool enable);
    void enableMeter(bool enable);
    void enableTimecode(bool enable);
    void setBackgroundColor(float r, float g, float b);
    void enableBorder(bool enable);
    void setBorderColor(const std::string& colorStr);
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }
    void setVideoEnabled(bool enabled) { m_videoEnabled = enabled; }

    bool m_audioEnabled = true;
    bool m_videoEnabled = true;

    // UI STATE (Public for Pure GPU Engine Access)
    std::string m_status = "none";
    std::string m_name = "";
    bool m_meterEnabled = false;
    bool m_timecodeEnabled = false;
    bool m_borderEnabled = false;
    GdkRGBA m_borderColor{ 211.0f/255.0f, 211.0f/255.0f, 211.0f/255.0f, 1.0f };
    float m_bgR = 0, m_bgG = 0, m_bgB = 0;
    float m_peakL = 0, m_peakR = 0;
    float m_peakHoldL = 0, m_peakHoldR = 0;
    int m_peakTimerL = 0, m_peakTimerR = 0;
    std::string m_currentTimecode = "";
    std::mutex m_tcMutex;
    std::atomic<int> m_widgetW{480};
    std::atomic<int> m_widgetH{270};

private:
    GtkWidget* m_mainWidget = nullptr;    // GtkOverlay
    GtkWidget* m_videoPicture = nullptr;  // GtkPicture
    GtkWidget* m_overlayWidget = nullptr; // SAF GPU WIDGET

    // Image Processing
    unsigned char* m_rgbBuffer = nullptr;
    int m_bufferW = 0, m_bufferH = 0;
    std::mutex m_bufferMutex;
    struct SwsContext* m_swsCtx = nullptr;

    // Audio Engine
    SDL_AudioDeviceID m_audioDeviceID = 0;
    struct SwrContext* m_swrCtx = nullptr;
    uint8_t* m_audioResampleBuf = nullptr;
    int m_lastSampleRate = 0, m_lastChannels = 0;
    AVSampleFormat m_lastFormat = AV_SAMPLE_FMT_NONE;

    // Logic
    LObject* m_activeSource = nullptr;
    std::thread m_workerThread;
    bool m_running = false;
    std::queue<AVFrame*> m_vQueue;
    std::mutex m_qMutex;
    
    void converterLoop();
    void updateUI();
};

#endif
