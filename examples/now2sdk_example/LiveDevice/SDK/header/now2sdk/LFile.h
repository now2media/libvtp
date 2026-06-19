#ifndef LFILE_H
#define LFILE_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>
#include "LFormat.h"
#include "LObject.h"
#include "LFilter.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

class LFile : public LObject {
public:
    LFile();
    ~LFile();

    // 1. Oynatım Kontrolleri
    void fileNameSet(const std::string& filePath);
    bool play();
    void pause(double ms = 0); // ms > 0 ise otomatik devam eder
    void resume();
    void stop();
    void statusGet(int& status) override;
    
    bool isPaused() const { return paused; }
    bool isRunning() const { return running; }
    bool isEOF() const { return m_eofReached; }

    // 2. Medialooks Tarzı Kontroller (Milisaniye Bazlı)
    void setPos(double ms);
    double getPos();
    void setPosTC(const std::string& tc);
    std::string getPosTC();
    
    // Backward compatibility wrappers
    void PosSet(double ms) { setPos(ms); }
    double PosGet() { return getPos(); }
    
    void RateSet(double rate);
    double RateGet() { return playbackRate; }
    void InOutSet(double inMs, double outMs);
    void InOutGet(double& inMs, double& outMs, double& durationMs);
    std::string getInTC();
    std::string getOutTC();

    // 3. Esnek Özellikler (setProps)
    void setProps(const std::string& key, const std::string& value);
    void setAudioDelay(int ms);
    void setVideoDelay(int ms);

    // 4. Bilgi Alma (Getters)
    double getDurationMs();
    double getFPS() override;
    std::string getTimeCode() override;
    int getWidth() { return width; }
    int getHeight() { return height; }
    void getAudioPeak(float& left, float& right);
    void resetAudioPeak();

    void setVideoFilter(LFilter* filter);
    void setAudioFilter(LFilter* filter);
    
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);


private:
    void decodeLoopInternal(bool pauseOnFirstFrame);
    bool openFile();
    bool initHWDecoder(AVCodecContext* ctx);
    bool initAudioFilters(const std::string& filterStr);
    void switchAudioTrack(int trackIndex);

    std::string filePath;
    std::thread decodeThread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVCodecContext* audioCodecCtx = nullptr;
    struct SwrContext* swrCtx = nullptr;
    AVFilterGraph* audioFilterGraph = nullptr;
    AVFilterContext* audioSrcCtx = nullptr;
    AVFilterContext* audioSinkCtx = nullptr;
    
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    double m_sourceFps = 25.0;
    int width = 0;
    int height = 0;
    
    
    const int MAX_VIDEO_QUEUE = 15;
    
    uint8_t* audioResampleBuf = nullptr;
    
    // Properties & State
    double playbackRate = 1.0;
    double inPointMs = 0;
    double outPointMs = 0;
    double seekTargetMs = -1;
    bool seekRequested = false;
    bool loopEnabled = false;
    int m_audioDelayMs = 0;
    int m_videoDelayMs = 0;
    bool eofHold = true;
    std::atomic<bool> m_eofReached{false};
    bool deinterlaceEnabled = false;
    bool m_gpuEnabled = false;
    
    int scalingQuality = 0;
    int swsFlags = 0;
    std::string scaleType = "letter-box";
    
    int audioGain = 100;
    int targetAudioTrack = 0;
    bool audioOutputEnabled = true;
    
    int currentPositionFrames = 0;
    
    // Timecode desteği
    std::string m_startTimecode = "00:00:00:00";
    bool m_hasTimecode = false;
    int m_timeCodeSource = 1; // 0: Sistem Saati, 1: Kaynaktan Gelen
    
    void ready();

    float getPeakL() { return peakL; }
    float getPeakR() { return peakR; }
    int getScalingQuality() const { return scalingQuality; }
    int getSwsFlags() const { return swsFlags; }
    std::string getScaleType() const { return scaleType; }

    videoFormatProps targetVideoProps;
    audioFormatProps targetAudioProps;

    void updateAudioPeaks(uint8_t** audioData, int samples);

    // Senkronizasyon için
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point pauseStartTime;
    bool resyncRequested = false;

    // Otomatik duraklatma için
    std::chrono::steady_clock::time_point pauseUntil;
    bool autoResumeActive = false;

    LFilter* m_videoFilter = nullptr;
    LFilter* m_audioFilter = nullptr;
    std::string m_currentAudioFilterDesc = "";
};

#endif // LFILE_H
