#pragma once
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/rational.h>
}
#include "LObject.h"
#include "LFormat.h"
#include "now2sdk_global.h"
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <chrono>
#include <deque>
#include <vector>

class LCharacter;

class SwitcherSink : public LSink {
    friend class LSwitcher; // LSwitcher'ın private değişkenlere erişebilmesi için
public:
    SwitcherSink(int targetSampleRate = 48000) : m_lastFrame(nullptr), m_swr(nullptr), m_resampleBuf(nullptr), m_targetSampleRate(targetSampleRate) {
        av_channel_layout_default(&outChLayout, 2);
    }
    virtual ~SwitcherSink() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        if (m_swr) swr_free(&m_swr);
        if (m_resampleBuf) av_freep(&m_resampleBuf);
        if (m_cgSws) sws_freeContext(m_cgSws);
        if (m_cgScaledFrame) av_frame_free(&m_cgScaledFrame);
    }
    virtual void pushVideoFrame(AVFrame* frame) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        m_lastFrame = av_frame_clone(frame);
        m_lastFrameTime = std::chrono::steady_clock::now();
        
// Clean up old audio data (not needed for PTS-based history)
// int maxSamples = m_targetSampleRate * 2 * 0.1; 
// if (m_audioBuffer.size() > (size_t)maxSamples) {
//     m_audioBuffer.erase(m_audioBuffer.begin(), m_audioBuffer.end() - (maxSamples / 2));
// }
    }
    struct AudioRecord {
        double ptsSec;
        std::vector<int16_t> samples;
    };
    std::deque<AudioRecord> m_audioHistory;

    virtual void pushAudioFrame(AVFrame* frame) override {
        if (!frame || frame->nb_samples <= 0) return;
        std::lock_guard<std::mutex> lock(m_mutex);
        
        double ptsSec = 0.0;
        if (frame->pts != AV_NOPTS_VALUE) {
            ptsSec = frame->pts * av_q2d(frame->time_base);
        }

        std::vector<int16_t> out_buf;
        if (frame->sample_rate != m_targetSampleRate || (AVSampleFormat)frame->format != AV_SAMPLE_FMT_S16 || frame->ch_layout.nb_channels != 2) {
            if (!m_swr) {
                int ret = swr_alloc_set_opts2(&m_swr, &outChLayout, AV_SAMPLE_FMT_S16, m_targetSampleRate,
                                    &frame->ch_layout, (AVSampleFormat)frame->format, frame->sample_rate, 0, nullptr);
                if (ret < 0 || swr_init(m_swr) < 0) {
                    if (m_swr) swr_free(&m_swr);
                    return;
                }
            }
            
            int out_samples = av_rescale_rnd(swr_get_delay(m_swr, frame->sample_rate) + frame->nb_samples, m_targetSampleRate, frame->sample_rate, AV_ROUND_UP);
            out_buf.resize(out_samples * 2);
            uint8_t* out_ptr = (uint8_t*)out_buf.data();
            int converted = swr_convert(m_swr, &out_ptr, out_samples, (const uint8_t**)frame->data, frame->nb_samples);
            
            if (converted > 0) {
                out_buf.resize(converted * 2);
            } else return;
        } else {
            int16_t* samples = (int16_t*)frame->data[0];
            out_buf.assign(samples, samples + frame->nb_samples * 2);
        }
        
        m_audioHistory.push_back({ptsSec, std::move(out_buf)});
        
        // RAM güvenliği: maks 5000 paket (~100 saniye tutar)
        if (m_audioHistory.size() > 5000) {
            m_audioHistory.pop_front();
        }
    }
    
    AVFrame* getLatestFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_lastFrame) return nullptr;
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - m_lastFrameTime).count() > 1.0) {
            return nullptr; 
        }
        
        return av_frame_clone(m_lastFrame);
    }
    
    int popAudioSamplesForPts(double currentVideoPts, int16_t* dest, int count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        memset(dest, 0, count * 2 * sizeof(int16_t));
        int copied = 0;
        
        while (!m_audioHistory.empty() && copied < count) {
            // LPreview mantığı: Sesin PTS'i video PTS'ine yetişmişse kullan
            if (m_audioHistory.front().ptsSec <= currentVideoPts + 0.05) {
                auto& packet = m_audioHistory.front();
                int available = packet.samples.size() / 2;
                int toCopy = std::min(count - copied, available);
                
                memcpy(dest + (copied * 2), packet.samples.data(), toCopy * 2 * sizeof(int16_t));
                copied += toCopy;
                
                if (toCopy < available) {
                    packet.samples.erase(packet.samples.begin(), packet.samples.begin() + (toCopy * 2));
                    packet.ptsSec += (double)toCopy / m_targetSampleRate; // PTS'i ilerlet
                    break;
                } else {
                    m_audioHistory.pop_front();
                }
            } else {
                // Bu ses paketi geleceğe ait (video geride), o yüzden beklet
                break;
            }
        }
        return count; // Eksik kalan yerler memset ile zaten sıfırlandı (sessizlik)
    }

    int popAudioSamples(int16_t* dest, int count) {
        return popAudioSamplesForPts(0.0, dest, count); // Geriye dönük uyumluluk
    }
    void clearQueue() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioHistory.clear();
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        m_lastFrame = nullptr;
    }
    
    void flush() override {
        clearQueue();
    }
    
    int getBufferSizeMs() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_targetSampleRate <= 0) return 0;
        size_t totalSamples = 0;
        for (const auto& pkt : m_audioHistory) totalSamples += pkt.samples.size() / 2;
        return (int)((totalSamples) / (m_targetSampleRate / 1000.0));
    }
private:
    AVChannelLayout outChLayout;
    AVFrame* m_lastFrame;
    SwrContext* m_swr;
    uint8_t*    m_resampleBuf;
    int         m_targetSampleRate;
    std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastFrameTime;
    
    // CG Katmanları için otomatik boyutlandırma önbelleği
    struct SwsContext* m_cgSws = nullptr;
    struct AVFrame* m_cgScaledFrame = nullptr;
};

struct SwitcherLayer {
    LObject*    pObj        = nullptr;
    SwitcherSink*  pSink       = nullptr;
};

class NOW2SDK_EXPORT LSwitcher : public LObject {
public:
    LSwitcher();
    virtual ~LSwitcher();

    // Licensing
    bool setLicense(const std::string& licenseKey);
    bool isLicensed() const;

    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);
    void setProps(const std::string& key, const std::string& value);

    virtual std::string getTimeCode() override;

    void setPreview(LObject* obj);
    void setProgram(LObject* obj);
    void setTransition(float progress);
    void setAutoTransition(double durationMs);
    void setCutTransition();
    void setTransitionType(const std::string& type, const std::string& arg = "");
    void setProgAudioGain(int gain);
    void setPrevAudioGain(int gain);
    
    // Stinger
    void setStinger(const std::string& path);
    void setStingerDuration(int ms);
    void triggerStinger();

    // CG (Character Generator)
    void addCG(LCharacter* cg);
    void removeCG(LCharacter* cg);
    int getCGCount() const;
    LCharacter* getCGByIndex(int index);

    struct SwitcherStats {
        double fps;
        std::string onAir;
        float progress;
        int64_t frameCount;
        std::string timecode;
        float audioLevelA;
        float audioLevelB;
        float audioLevelOut;
        int audioBufferMs;
        double avSyncMs;
    };
    void getStats(SwitcherStats& stats);

private:
    void switcherLoop();
    void ensureRunning();
    void cleanupLayer(SwitcherLayer& layer);
    void applyLayer(SwitcherLayer& layer, LObject* obj);
    AVFrame* getLayerFrame(SwitcherLayer& layer);

    mutable std::recursive_mutex m_switcherMutex;
    std::thread m_switcherThread;
    std::atomic<bool> m_running;

    SwitcherLayer m_subA; 
    SwitcherLayer m_subB; 
    
    struct AVFrame* m_lastFrameA = nullptr;
    struct AVFrame* m_lastFrameB = nullptr;
    struct SwsContext* m_swsA = nullptr;
    struct SwsContext* m_swsB = nullptr;
    int m_lastAW = 0, m_lastAH = 0, m_lastBW = 0, m_lastBH = 0;
    int m_lastADW = 0, m_lastADH = 0, m_lastBDW = 0, m_lastBDH = 0;
    
    videoFormatProps targetVideoProps;
    audioFormatProps targetAudioProps;
    float m_mixValue = 0.0f;
    int64_t m_frameCount = 0;
    int64_t m_totalSamplesOut = 0;
    std::string m_transType     = "cut";
    std::string m_transArg      = "";
    uint8_t     m_dipY          = 16;
    uint8_t     m_dipU          = 128;
    uint8_t     m_dipV          = 128;
    bool        m_keepAudio     = true;

    int m_progGain = 100; 
    int m_prevGain = 0;   
    float m_volA = 1.0f;  
    float m_volB = 0.0f;  
    int m_masterGain = 100;

    int m_scalingQuality = 2; // SWS_BILINEAR
    int m_scaleMode      = 0; // 0: Stretch, 1: Fit, 2: Fill
    double m_actualFps   = 0.0;
    std::chrono::steady_clock::time_point m_lastFpsTime;
    int m_fpsFrames = 0;

    float m_peakA = 0.0f;
    float m_peakB = 0.0f;
    float m_peakOut = 0.0f;
    int   m_audioBufferMs = 0;

    void updateAudioVolumes();

    bool        m_inAutoTransition = false;
    double      m_autoDurationMs   = 0.0;
    float       m_autoStartValue   = 0.0f;
    float       m_autoTargetValue  = 0.0f;
    std::chrono::steady_clock::time_point m_autoStartTime;
    
    // Stinger Internal
    std::string m_stingerPath;
    int         m_stingerDurationMs = 1000;
    int         m_stingerCutPointMs = 500;
    bool        m_stingerActive     = false;
    bool        m_stingerSwitched   = false;
    class LStinger* m_stinger       = nullptr;
    class SwitcherSink* m_stingerSink = nullptr;
    std::chrono::steady_clock::time_point m_stingerStartTime;

    // CG Layers
    std::vector<LCharacter*> m_cgLayers;
    std::vector<SwitcherSink*> m_cgSinks;

    bool        m_timeCodeEnabled  = false;
    int         m_timeCodeSource   = 1; // 0: System, 1: Internal
    std::string getTimecodeString(int64_t frameCount, double fps);
};
