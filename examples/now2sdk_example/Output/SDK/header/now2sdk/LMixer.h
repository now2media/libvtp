#pragma once
extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}
#include "LObject.h"
#include "LFormat.h"
#include "now2sdk_global.h"
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <chrono>
#include <map>
#include <vector>
#include <deque>
#include <limits>

class MixerSink : public LSink {
public:
    MixerSink(int targetSampleRate = 48000) : m_lastFrame(nullptr), m_swr(nullptr), m_targetSampleRate(targetSampleRate) {
        av_channel_layout_default(&outChLayout, 2);
    }
    virtual ~MixerSink() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        if (m_swr) swr_free(&m_swr);
    }
    virtual void pushVideoFrame(AVFrame* frame) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        m_lastFrame = av_frame_clone(frame);
        m_lastFrameTime = std::chrono::steady_clock::now();
    }

    struct AudioRecord {
        double ptsSec;
        std::vector<int16_t> samples;
    };
    std::deque<AudioRecord> m_audioHistory;

    virtual void pushAudioFrame(AVFrame* frame) override {
        if (!frame || frame->nb_samples <= 0) return;
        std::lock_guard<std::mutex> lock(m_mutex);

        // PTS hesapla (SwitcherSink ile aynı mantık)
        double ptsSec = 0.0;
        if (frame->pts != AV_NOPTS_VALUE) {
            ptsSec = frame->pts * av_q2d(frame->time_base);
        }

        std::vector<int16_t> out_buf;
        // Ses formatı farklıysa çevir (48000 Hz, Stereo, S16)
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
            if (converted > 0) out_buf.resize(converted * 2);
            else return;
        } else {
            int16_t* samples = (int16_t*)frame->data[0];
            out_buf.assign(samples, samples + frame->nb_samples * 2);
        }

        m_audioHistory.push_back({ptsSec, std::move(out_buf)});
        // RAM güvenliği: maks 5000 paket
        if (m_audioHistory.size() > 5000) m_audioHistory.pop_front();
    }
    
    AVFrame* getLatestFrame() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_lastFrame) return nullptr;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - m_lastFrameTime).count() > 1.0) return nullptr;
        return av_frame_clone(m_lastFrame);
    }

    // SwitcherSink ile aynı mantık: video PTS'e göre ses çek
    int popAudioSamplesForPts(double currentVideoPts, int16_t* dest, int count) {
        std::lock_guard<std::mutex> lock(m_mutex);
        memset(dest, 0, count * 2 * sizeof(int16_t));
        int copied = 0;
        while (!m_audioHistory.empty() && copied < count) {
            if (m_audioHistory.front().ptsSec <= currentVideoPts + 0.05) {
                auto& packet = m_audioHistory.front();
                int available = packet.samples.size() / 2;
                int toCopy = std::min(count - copied, available);
                memcpy(dest + (copied * 2), packet.samples.data(), toCopy * 2 * sizeof(int16_t));
                copied += toCopy;
                if (toCopy < available) {
                    packet.samples.erase(packet.samples.begin(), packet.samples.begin() + (toCopy * 2));
                    packet.ptsSec += (double)toCopy / m_targetSampleRate;
                    break;
                } else {
                    m_audioHistory.pop_front();
                }
            } else {
                break;
            }
        }
        return count;
    }

    int popAudioSamples(int16_t* dest, int count) {
        return popAudioSamplesForPts(std::numeric_limits<double>::max(), dest, count);
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_audioHistory.clear();
        if (m_lastFrame) av_frame_free(&m_lastFrame);
        m_lastFrame = nullptr;
    }

private:
    AVChannelLayout outChLayout;
    AVFrame* m_lastFrame;
    SwrContext* m_swr;
    int m_targetSampleRate;
    std::mutex m_mutex;
    std::chrono::steady_clock::time_point m_lastFrameTime;
};

class LLayerItem {
public:
    std::string layerID;
    LObject* pObj = nullptr;
    MixerSink* pSink = nullptr;
    
    // Properties
    bool show = true;
    int alpha = 255; 
    float posX = 0.0f;
    float posY = 0.0f;
    float width = -1.0f; 
    float height = -1.0f; 
    float cropTop = 0.0f;
    float cropBottom = 0.0f;
    float cropLeft = 0.0f;
    float cropRight = 0.0f;
    int borderSize = 0;
    std::string borderColor = "#FFFFFF";
    int borderAlpha = 255; // Kenarlık şeffaflığı (0-255)
    int borderRadius = 0;  // Kenarlık yuvarlaklığı (Yüzde 0-100)
    bool audio = false;
    int gain = 100;
    int zIndex = 0;
    
    struct AVFrame* pLastFrame = nullptr;
    struct AVFrame* pScaledFrame = nullptr; // Bellek yorulmasını önlemek için önbellek
    struct SwsContext* pSwsContext = nullptr;
    int lastSrcW = 0, lastSrcH = 0, lastDstW = 0, lastDstH = 0, lastDstFmt = 0;

    unsigned int texY = 0;
    unsigned int texU = 0;
    unsigned int texV = 0;
    unsigned int texA = 0;
};

class NOW2SDK_EXPORT LMixer : public LObject {
public:
    LMixer();
    virtual ~LMixer();

    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);

    LLayerItem* addLayer(const std::string& layerID, LObject* obj, 
                         int zIndex = 0,
                         float posX = 0.0f, float posY = 0.0f, 
                         int alpha = 255, 
                         float width = -1.0f, float height = -1.0f,
                         float cropTop = 0.0f, float cropBottom = 0.0f, float cropLeft = 0.0f, float cropRight = 0.0f,
                         int borderSize = 0, const std::string& borderColor = "#FFFFFF",
                         bool audio = false, int gain = 100);

    LLayerItem* addImage(const std::string& layerID, const std::string& filePath,
                         int zIndex = 0,
                         float posX = 0.0f, float posY = 0.0f,
                         int alpha = 255,
                         float width = -1.0f, float height = -1.0f,
                         float cropTop = 0.0f, float cropBottom = 0.0f, float cropLeft = 0.0f, float cropRight = 0.0f,
                         int borderSize = 0, const std::string& borderColor = "#FFFFFF");
    
    LLayerItem* getLayer(const std::string& layerID);
    void removeLayer(const std::string& layerID);
    void setLayerBorderRadius(const std::string& layerID, int radiusPercent);
    
    // Katman Döngüsü için
    int getLayerCount() const;
    LLayerItem* getLayerByIndex(int index);

private:
    void mixerLoop();
    void ensureRunning();
    void cleanupLayerItem(LLayerItem* item);

    mutable std::recursive_mutex m_mixerMutex;
    std::thread m_mixerThread;
    std::atomic<bool> m_running;

    std::map<std::string, LLayerItem*> m_layers;
    videoFormatProps targetVideoProps;
    audioFormatProps targetAudioProps;

    void* m_glContext = nullptr;      // QOpenGLContext*
    void* m_glSurface = nullptr;      // QOffscreenSurface*
    unsigned int m_fbo = 0;
    unsigned int m_fboTex = 0;
    unsigned int m_program = 0;
    unsigned int m_dummyTexA = 0;
    struct SwsContext* m_rgbaToYuvSws = nullptr;

    std::vector<unsigned int> m_texturesToDelete;
    std::mutex m_deleteMutex;
};
