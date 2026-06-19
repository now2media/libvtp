#pragma once
#include "LSink.h"
#include "now2sdk_global.h"
#include <string>
#include <mutex>
#include <vector>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>
#include "LFormat.h"

// DeckLink ve NDI başlıkları SADECE LOutput.cpp içinde kullanılır.
// Kullanıcıların header/decklink veya header/ndi yollarına ihtiyacı YOKTUR.

// DeckLink forward declarations (pointer olarak sakladığımız için yeterli)
class IDeckLink;

struct SwsContext;
struct SwrContext;
class LObject;

class NOW2SDK_EXPORT LOutput : public LSink {
public:
    LOutput();
    virtual ~LOutput();

    // Çıkış Kontrolü (Yeni Yapı)
    void setEnabled(bool enabled);
    void setProps(const std::string& key, const std::string& value);
    void setDevice(int index);
    
    void DeviceFormatAudioSet(const audioFormatProps& props);

    // LSink Arayüzü (Kareler buraya gelecek)
    virtual void pushVideoFrame(struct AVFrame* frame) override;
    virtual void pushAudioFrame(struct AVFrame* frame) override;

    // Kaynağa Bağlanma Yardımcısı
    void setSource(LObject* source);

    // Cihaz ve Format Sorgulama (LLive benzeri)
    void DeviceGetCount(int& count);
    void DeviceGetByIndex(int index, std::string& name, std::string& desc);
    void DeviceChannelGetCount(int deviceIndex, int& count);
    void DeviceChannelGetByIndex(int deviceIndex, int channelIndex, std::string& name, std::string& desc);
    void DeviceChannelSet(int index);
    void DeviceFormatVideoGetCount(int deviceIndex, int channelIndex, int& count);
    void DeviceFormatVideoGetByIndex(int deviceIndex, int channelIndex, int formatIndex, std::string& name, videoFormatProps& props);
    void DeviceFormatVideoSet(int index);

    struct OutputStats {
        int width;
        int height;
        double fps;
        int audioChannels;
        int audioSampleRate;
        int droppedFrames;
        int bufferedFrames;
        int displayedFrames;
    };
    void getStats(OutputStats& stats);

private:
    std::mutex m_mutex;

    // NDI sender — void* olarak saklanır, .cpp'de cast edilir.
    // Bu sayede kullanıcının NDI başlık yoluna ihtiyacı kalmaz.
    void* m_ndiSend = nullptr; // NDIlib_send_instance_t

    struct SwsContext* m_swsContext = nullptr;
    LObject* m_currentSource = nullptr;

    // Persist buffer'lar (Çökme olmaması için)
    std::vector<uint8_t> m_videoBuffer;
    std::vector<float> m_audioBuffer;

    bool m_enabled = false;
    int m_selectedDeviceIndex = 0;
    int m_selectedChannelIndex = 0;
    int m_selectedFormatIndex = 0;

    int m_targetWidth = 1920;
    int m_targetHeight = 1080;
    double m_targetFps = 25.0;
    
    int m_targetSampleRate = 48000;
    int m_targetChannels = 2;
    
    int m_colorFormat = 1; // 0: BGRA, 1: UYVY (Varsayılan UYVY)
    int m_deckLinkColorFormat = 1; // 0: BGRA, 1: UYVY (Varsayılan UYVY)
    bool m_formatChanged = false;
    std::string m_streamName = "NDI_Output";
    
    // Yeni Özellikler
    int m_scalingQuality = 2; // SWS_BILINEAR
    bool m_enableTimecode = false;
    std::string m_timecodeFormat = "auto";
    float m_audioGain = 1.0f;
    
    // İstatistikler
    std::atomic<int64_t> m_displayedFrames{0};
    std::atomic<int64_t> m_droppedFrames{0};

    // DeckLink Nesneleri (void* olarak saklanır, .cpp'de cast edilir)
    std::vector<IDeckLink*> m_deckLinkList;
    std::vector<void*> m_deckLinkDisplayModes; // IDeckLinkDisplayMode*
    std::vector<std::string> m_videoLines;
    void* m_deckLinkOutput = nullptr; // IDeckLinkOutput*

    struct SwrContext* m_swrAudio = nullptr;
    struct SwrContext* m_swrHardware = nullptr;
    int m_currentInRate{0};
    int m_currentInFmt{-1};
    int m_currentInChannels{0};

    int m_lastInWidth{0};
    int m_lastInHeight{0};
    int m_lastInFormat{-1};

    void updateDeckLinkLines(int deviceIndex);
    bool startDeckLink();
    bool stopDeckLink();

    // Asenkron Yapı (LVideoWidget benzeri)
    struct AudioRecord {
        double ptsSec;
        std::vector<int16_t> samples;
    };
    std::deque<AudioRecord> m_audioHistory;
    std::mutex m_audioHistoryMutex;

    std::queue<struct AVFrame*> m_videoQueue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCond;
    std::thread m_outputThread;
    std::atomic<bool> m_threadRunning{false};
    std::thread m_audioThread;
    std::atomic<bool> m_audioThreadRunning{false};
    std::atomic<double> m_currentVideoPts{0.0};

    void outputLoop();
    void audioLoop();
    int popAudioSamplesForPts(double currentVideoPts, int16_t* dest, int count);
    
    // NDI ve DeckLink başlatma yardımcıları
    bool startNDI();
    void stopNDI();

    // VTP başlatma yardımcıları
    void* m_vtpSend = nullptr; // vtp_sender_t*
    bool startVTP();
    void stopVTP();
};
