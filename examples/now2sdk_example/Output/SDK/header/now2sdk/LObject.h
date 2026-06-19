#pragma once
#include <mutex>
#include <string>
#include <chrono>
#include <condition_variable>
#include <vector>
#include <algorithm>
#include <deque>
#include <atomic>

struct AVFrame;
#include "LSink.h"

extern "C" {
#include <libavutil/frame.h>
}

// LPlatform'daki IMObject karşılığı olan temel Ata Sınıf (Base Class)
class LObject {
public:
    virtual ~LObject() {
        std::lock_guard<std::mutex> lock(queueMutex);
        for (auto f : videoQueue) av_frame_free(&f);
        videoQueue.clear();
    }

    // 1. Alıcı Yönetimi (Dışarıdan erişilmeli)
    void addSink(LSink* sink) {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        m_sinks.push_back(sink);
    }

    void removeSink(LSink* sink) {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        auto it = std::find(m_sinks.begin(), m_sinks.end(), sink);
        if (it != m_sinks.end()) m_sinks.erase(it);
    }

    void flushSinks() {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        for (auto sink : m_sinks) {
            if (sink) sink->flush();
        }
    }

    void setSinksPaused(bool paused) {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        for (auto sink : m_sinks) {
            if (sink) sink->setPaused(paused);
        }
    }

    void getAudioPeak(float& left, float& right) {
        left = peakL;
        right = peakR;
    }

    void resetAudioPeak() {
        peakL = 0;
        peakR = 0;
    }

    virtual std::string getTimeCode() { return "00:00:00:00"; }

    // 2. Durum Bilgisi (Kullanıcı için)
    virtual void statusGet(int& status) { status = 0; }
    virtual double getFPS() { return 25.0; }
    virtual int getWidth() { return 0; }
    virtual int getHeight() { return 0; }
    virtual std::string getModeName() { return "Default"; }

    bool noSignalGet() const { return m_noSignal; }
    void noSignalSet(bool noSignal) { m_noSignal = noSignal; }

protected:
    // Türetilen sınıfların (LFile, LLive vb.) kullandığı dahili metodlar
    void distributeVideoFrame(AVFrame* frame) {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        if (m_sinks.empty()) {
            std::lock_guard<std::mutex> qLock(queueMutex);
            videoQueue.push_back(av_frame_clone(frame));
            if (videoQueue.size() > 50) {
                AVFrame* f = videoQueue.front();
                videoQueue.pop_front();
                av_frame_free(&f);
            }
        } else {
            for (auto sink : m_sinks) {
                sink->pushVideoFrame(frame);
            }
        }
    }

    void distributeAudioFrame(AVFrame* frame) {
        std::lock_guard<std::mutex> lock(m_sinkMutex);
        for (auto sink : m_sinks) {
            sink->pushAudioFrame(frame);
        }
    }

    void calculatePeak(AVFrame* frame) {
        if (!frame || frame->nb_samples <= 0) return;
        float maxL = 0.0f, maxR = 0.0f;

        if (frame->format == AV_SAMPLE_FMT_FLTP) {
            float* dataL = (float*)frame->data[0];
            float* dataR = (frame->ch_layout.nb_channels > 1) ? (float*)frame->data[1] : dataL;
            for (int i = 0; i < frame->nb_samples; i += 4) {
                float sL = std::abs(dataL[i]);
                float sR = std::abs(dataR[i]);
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
            }
        } else if (frame->format == AV_SAMPLE_FMT_S16) {
            int16_t* data = (int16_t*)frame->data[0];
            for (int i = 0; i < frame->nb_samples; i += 4) {
                float sL = std::abs(data[i * 2]) / 32768.0f;
                float sR = std::abs(data[i * 2 + 1]) / 32768.0f;
                if (sL > maxL) maxL = sL;
                if (sR > maxR) maxR = sR;
            }
        }
        peakL = maxL; peakR = maxR;
    }

    AVFrame* getNextFrame() {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (videoQueue.empty()) return nullptr;
        AVFrame* f = videoQueue.front();
        videoQueue.pop_front();
        return f;
    }

protected:
    // Dahili Değişkenler
    std::deque<AVFrame*> videoQueue;
    std::mutex queueMutex;
    std::vector<LSink*> m_sinks;
    std::mutex m_sinkMutex;
    float peakL = 0.0f;
    float peakR = 0.0f;
    std::atomic<bool> m_noSignal{false};
};
