#ifndef LSTREAMPLAY_H
#define LSTREAMPLAY_H

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <deque>
#include <chrono>
#include "LFormat.h"
#include "LObject.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

class LStreamPlay : public LObject {
public:
    LStreamPlay();
    ~LStreamPlay();

    // 1. Playback Controls
    void streamUrlSet(const std::string& url);
    void fileNameSet(const std::string& url) { streamUrlSet(url); } // Alias for compatibility
    static bool checkStream(const std::string& url);
    
    bool play();
    void pause(double ms = 0); // ms > 0 auto-resumes
    void resume();
    void stop();
    void statusGet(int& status) override;
    bool isHealthy() const { return isStreamHealthy.load(); }

    // 2. Flexible Properties (setProps)
    void setProps(const std::string& key, const std::string& value);
    void setAudioDelay(int ms);
    void setVideoDelay(int ms);

    // 3. Information (Getters)
    double getFPS() override;
    std::string getTimeCode() override;
    int getWidth() { return width; }
    int getHeight() { return height; }
    void getAudioPeak(float& left, float& right);
    void resetAudioPeak();

    // 4. Formatting
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);

private:
    void decodeLoopInternal();
    bool openStream();
    void switchAudioTrack(int trackIndex);

    std::string streamUrl;
    std::thread decodeThread;
    std::atomic<bool> running{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> isStreamHealthy{false};
    
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVCodecContext* audioCodecCtx = nullptr;
    struct SwrContext* swrCtx = nullptr;
    
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    double m_sourceFps = 25.0;
    int width = 0;
    int height = 0;
    
    uint8_t* audioResampleBuf = nullptr;
    
    // Properties & State
    int m_audioDelayMs = 0;
    int m_videoDelayMs = 0;
    int scalingQuality = 0;
    int swsFlags = 0;
    std::string scaleType = "letter-box";
    
    int audioGain = 100;
    int targetAudioTrack = 0;
    bool audioOutputEnabled = true;
    
    // Sync mechanisms
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point pauseStartTime;
    std::chrono::steady_clock::time_point pauseUntil;
    bool autoResumeActive = false;

    videoFormatProps targetVideoProps;
    audioFormatProps targetAudioProps;

    void updateAudioPeaks(uint8_t** audioData, int samples);
    void ready();
};

#endif // LSTREAMPLAY_H
