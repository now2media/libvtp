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
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#include "LFormat.h"
#include "LSink.h"
#include <functional>

class LObject;

class LRecorder : public LSink {
public:
    LRecorder();
    ~LRecorder();

    // 1. Ayarlar (Format ve Codec Seçimi)
    void setContainer(const std::string& container); // Örn: "mp4", "mkv", "mov"
    void setVideoCodec(const std::string& codec);    // Örn: "libx264", "h264_nvenc"
    void setAudioCodec(const std::string& codec);    // Örn: "aac", "pcm_s16le"
    void setVideoBitrate(int bitrateKbps);
    void setAudioBitrate(int bitrateKbps);

    // Video/Audio fiziksel özellikleri
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);

    // 2. MPlatform tarzı Custom ve Props ayarları
    // Örn: "preset=fast tune=zerolatency g=30 bufsize=3000k"
    void customSet(const std::string& params);
    
    // Örn: "durationMs" = "10000", "play_while_rec" = "true", "scaling_quality" = "2", "timecode" = "0"
    void setProps(const std::string& key, const std::string& value);

    // 3. Kontrol İşlevleri
    void setFilePath(const std::string& filePath);
    void recordObject(LObject* source);
    bool Record();
    void Pause(bool pause);
    void Stop();
    void statusGet(int& status);

    // 4. İstatistik Çekme (Recording Stats)
    struct Stats {
        int64_t fileSizeBytes = 0;
        double durationWrittenMs = 0.0;
        int width = 0;
        int height = 0;
        double fps = 0.0;
        int audioChannels = 0;
        int audioSampleRate = 0;
        int droppedFrames = 0;
        int bufferedFrames = 0;    // Kuyrukta bekleyen kare sayısı
        double avSyncMs = 0.0;      // Ses ve Görüntü arasındaki kayma (ms)
        std::string timecode = ""; // O anki gerçek zaman kodu
    };
    void getStats(Stats& stats);
    
    // 5. Dinamik Sorgulama (UI İçin)
    static std::vector<std::string> getAvailableContainers();
    static std::vector<std::string> getAvailableVideoCodecs(const std::string& container);
    static std::vector<std::string> getAvailableAudioCodecs(const std::string& container);


private:
    std::string m_container;
    std::string m_videoCodec;
    std::string m_audioCodec;
    int m_videoBitrateKbps;
    int m_audioBitrateKbps;
    std::string m_filePath;

    videoFormatProps m_videoProps;
    audioFormatProps m_audioProps;
    
    std::map<std::string, std::string> m_customOptions;
    
    // Props
    double m_durationMs;
    bool m_playWhileRec;
    int m_scalingQuality;
    bool m_timeCodeEnabled;
    int m_timeCodeSource; // 0=Sistem Saati, 1=Kaynaktan Gelen, 2=Relative (timecodeStart)
    std::string m_timeCodeStart = "01:00:00:00";
    
    // Kaynak
    LObject* m_source = nullptr;

    // FFmpeg Objeleri
    AVFormatContext* m_fmtCtx;
    AVCodecContext* m_vCodecCtx;
    AVCodecContext* m_aCodecCtx;
    AVStream* m_vStream;
    AVStream* m_aStream;
    
    SwsContext* m_swsCtx;
    SwrContext* m_swrCtx;
    
    AVDictionary* m_vOptions;
    AVDictionary* m_aOptions;

    // Thread & Queue (Kasmayı önlemek için Asenkron yazma)
    std::atomic<bool> m_isRecording;
    std::atomic<bool> m_isPaused;
    std::thread m_recordThread;
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
    bool m_audioStarted; // Master Clock olarak Sesi kullanıyoruz
    int64_t m_recordStartTime; // Gerçek zaman (durationMs takibi için)
    int64_t m_pauseStartTime;  // Pause anındaki zaman damgası
    int64_t m_systemStartTime; // Timecode 0 için (Sistem Saati offset'i)
    int64_t m_lastVideoPts;    // Son yazılan video PTS (Frame Drop kontrolü için)

    // Audio history for PTS‑based sync
    std::mutex m_audioMutex;
    struct AudioRecord {
        double ptsSec;
        std::vector<int16_t> samples;
    };
    std::deque<AudioRecord> m_audioHistory;
    double m_currentSourceAudioPtsSec; // Kaynağın ses PTS takibi için
    int popAudioSamplesForPts(double videoPtsSec, int16_t* dest, int count);

    // İç İşlevler
    void encodeLoop();
    bool initFFmpeg();
    void writePacket(AVPacket* pkt, bool isVideo);
    
    bool m_timeCodeCaptured = false;
    std::string m_sourceTimeCode = "00:00:00:00";
    
    AVFrame* allocVideoFrame(int width, int height, AVPixelFormat format);
    AVFrame* allocAudioFrame(int channels, int sampleRate, AVSampleFormat format, int nb_samples);

    // LSink Arayüzü Uygulaması (Engine tarafından kullanılır, kullanıcı çağırmaz)
    void pushVideoFrame(AVFrame* frame) override;
    void pushAudioFrame(AVFrame* frame) override;
    void flush() override;
    void setPaused(bool paused) override { Pause(paused); }
};
