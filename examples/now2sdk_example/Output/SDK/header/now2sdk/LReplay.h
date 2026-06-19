#ifndef LREPLAY_H
#define LREPLAY_H

#include "LObject.h"
#include "LSink.h"
#include "LFormat.h"
#include "now2sdk_global.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <stdio.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/time.h>
}

class NOW2SDK_EXPORT LReplay : public LObject, public LSink {
public:
    LReplay();
    virtual ~LReplay();

    // Licensing
    bool setLicense(const std::string& licenseKey);
    bool isLicensed() const;

    // 1. Replay Setup & Recording (LSink)
    void setRecordPath(const std::string& directory, const std::string& prefix = "replay");
    void setExportPath(const std::string& directory, const std::string& prefix = "export");
    void setVideoFormat(const videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void setProps(const std::string& key, const std::string& value); // e.g. timecode, quality
    
    bool startRecording();
    void stopRecording();
    void clearRecording();



    // 2. Playback Engine (LObject)
    void play();
    void pause();
    void setModeLive();
    void setModeTrim();
    void setSpeed(double speed); // 1.0, 0.5, -0.5, -1.0, etc.
    void posSet(int frameIndex);
    void posSetMs(double ms);
    void posSetTC(const std::string& tc);
    void stepForward(int frames = 1);
    void stepBackward(int frames = 1);
    
    // Status
    int getTotalFrames();
    double getDurationMs();
    int getCurrentFrame();
    int getPos();
    double getPosMs();
    std::string getPosTC();

    // 3. Fast Export (No Re-encoding Muxing)
    bool exportRange(int inFrame, int outFrame, const std::string& outFilePath = "");
    bool exportRangeMs(double inMs, double outMs, const std::string& outFilePath = "");

    // LObject Overrides
    virtual void statusGet(int& status) override;
    virtual double getFPS() override;
    virtual int getWidth() override;
    virtual int getHeight() override;
    virtual std::string getTimeCode() override;
    virtual std::string getModeName() override { return "Replay"; }

private:
    // LSink Overrides (Internal)
    virtual void pushVideoFrame(AVFrame* frame) override;
    virtual void pushAudioFrame(AVFrame* frame) override;
    virtual void flush() override;
    virtual void setPaused(bool paused) override;

    struct FrameIndex {
        uint64_t offset;
        uint32_t size;
        double pts;
        char timecode[12]; // "HH:MM:SS:FF\0"
    };

    struct AudioIndex {
        uint64_t offset;
        uint32_t size;
        double pts;
    };

    std::string m_dir;
    std::string m_prefix;
    
    std::string m_exportDir = "/tmp/";
    std::string m_exportPrefix = "export";
    
    std::string m_currentVideoPath;
    std::string m_currentAudioPath;
    
    FILE* m_fileVideoWrite = nullptr;
    FILE* m_fileAudioWrite = nullptr;
    
    FILE* m_fileVideoRead = nullptr; // Separate handle for playback
    FILE* m_fileAudioRead = nullptr;
    
    std::vector<FrameIndex> m_frameIndex;
    std::vector<AudioIndex> m_audioIndex;
    std::mutex m_indexMutex;

    // FFmpeg Encoders
    AVCodecContext* m_mjpegCtx = nullptr;
    int m_quality = 86; // %
    
    videoFormatProps m_videoProps;
    audioFormatProps m_audioProps;
    
    std::string m_timeCode = "";
    std::string m_timeCodeStart = "01:00:00:00";
    bool m_timeCodeEnabled = false;
    int m_timeCodeSource = 0; // 0=System Time, 1=Source/Custom, 2=Relative (timecodeStart)
    
    std::string m_name = "";
    bool m_audioPreview = true; // false: ses sadece .bin'e kaydedilir, önizlemede çıkmaz
    
    // Engine State
    std::atomic<bool> m_isRecording{false};
    int64_t m_recordStartTime = 0;
    int64_t m_lastVideoPts = -1;
    
    std::thread m_playThread;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_isLive{false};
    std::atomic<bool> m_running{true};
    std::atomic<double> m_firstFramePtsSec{-1.0};
    std::atomic<double> m_speed{1.0};
    std::atomic<int> m_currentFrame{0};
    std::atomic<bool> m_stepRequested{false};
    int m_stepDirection = 0; // 1 for forward, -1 for backward
    
    // Decoding
    AVCodecContext* m_mjpegDecCtx = nullptr;
    struct SwsContext* m_swsCtx = nullptr;
    AVFrame* m_outFrame = nullptr;
    AVFrame* m_outYuvFrame = nullptr;
    
    // Encoding Conversion
    struct SwsContext* m_encodeSwsCtx = nullptr;
    AVFrame* m_encodeFrame = nullptr;
    
    void playbackLoop();
    bool initEncoder(int width, int height);
    bool initDecoder();
};

#endif // LREPLAY_H
