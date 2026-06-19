#pragma once
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <map>
#include <atomic>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libavutil/audio_fifo.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "LFormat.h"
#include "LSink.h"
#include <functional>

class LObject;

class LStream : public LSink {
public:
    LStream();
    ~LStream();

    // 1. Ayarlar (Yayın Codec Seçimi)
    void setVideoCodec(const std::string& codec);    // Örn: "libx264", "h264_nvenc"
    void setAudioCodec(const std::string& codec);    // Örn: "aac"
    void setVideoBitrate(int bitrateKbps);
    void setAudioBitrate(int bitrateKbps);

    // Video/Audio fiziksel özellikleri
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);

    // 2. Yayın Bilgileri
    void setStreamUrl(const std::string& url);
    void setStreamKey(const std::string& key);
    void setStreamClone(bool enable);
    void setStreamClonePath(const std::string& path);
    
    // MPlatform tarzı Custom ve Props ayarları
    void customSet(const std::string& params);
    void setProps(const std::string& key, const std::string& value);

    // 3. Kontrol İşlevleri
    void streamObject(LObject* source);
    bool Start();
    void Stop();
    void statusGet(int& status);

    // 4. İstatistik Çekme (Streaming Stats)
    struct Stats {
        double bitrateMbps = 0.0;
        int64_t totalBytesSent = 0;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        int droppedFrames = 0;
        int bufferedFrames = 0;
        std::string streamUrl = "";
        bool isConnected = false;
    };
    void getStats(Stats& stats);

private:
    std::string m_url;
    std::string m_streamKey;
    bool m_streamClone = false;
    std::string m_streamClonePath;
    std::string m_videoCodec;
    std::string m_audioCodec;
    int m_videoBitrateKbps;
    int m_audioBitrateKbps;

    videoFormatProps m_videoProps;
    audioFormatProps m_audioProps;
    
    std::map<std::string, std::string> m_customOptions;
    
    // Kaynak
    LObject* m_source = nullptr;

    // FFmpeg Objeleri
    AVFormatContext* m_fmtCtx = nullptr;
    AVFormatContext* m_cloneFmtCtx = nullptr;
    AVCodecContext* m_vCodecCtx = nullptr;
    AVCodecContext* m_aCodecCtx = nullptr;
    AVStream* m_vStream = nullptr;
    AVStream* m_aStream = nullptr;
    
    SwsContext* m_swsCtx = nullptr;
    SwrContext* m_swrCtx = nullptr;
    bool m_headerWritten = false;
    bool m_cloneHeaderWritten = false;

    // Thread & Queue
    std::atomic<bool> m_isStreaming;
    std::atomic<bool> m_isPaused{false};
    std::thread m_streamThread;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCond;
    
    struct FramePacket {
        AVFrame* frame;
        bool isVideo;
    };
    std::queue<FramePacket> m_frameQueue;

    std::atomic<int64_t> m_vFrameCount;
    std::atomic<int64_t> m_aSampleCount;
    std::atomic<int64_t> m_droppedFrames;
    bool m_audioStarted;
    int64_t m_streamStartTime;
    int64_t m_lastVideoPts;
    
    // Audio history for PTS‑based sync
    std::mutex m_audioMutex;
    struct AudioRecord {
        double ptsSec;
        std::vector<int16_t> samples;
    };
    std::deque<AudioRecord> m_audioHistory;
    double m_currentSourceAudioPtsSec;
    int popAudioSamplesForPts(double videoPtsSec, int16_t* dest, int count);
    
    // İstatistik için
    int64_t m_bytesSent = 0;
    int64_t m_lastStatTime = 0;
    double m_currentBitrateMbps = 0.0;

    // İç İşlevler
    void streamLoop();
    bool initFFmpeg();
    
    AVFrame* allocVideoFrame(int width, int height, AVPixelFormat format);
    AVFrame* allocAudioFrame(int channels, int sampleRate, AVSampleFormat format, int nb_samples);

    // LSink Arayüzü Uygulaması
    void pushVideoFrame(AVFrame* frame) override;
    void pushAudioFrame(AVFrame* frame) override;
    void flush() override;
    void setPaused(bool paused) override;
};
