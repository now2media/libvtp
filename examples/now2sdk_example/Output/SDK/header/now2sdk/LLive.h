#pragma once
#include "LObject.h"
#include "LFormat.h"
#include "LFilter.h"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <deque>
#include <condition_variable>

// DeckLink ve NDI başlıkları SADECE .cpp içinde kullanılır.
// Kullanıcıların header/decklink veya header/ndi yollarına ihtiyacı YOKTUR.

// DeckLink forward declarations (pointer olarak sakladığımız için yeterli)
class IDeckLink;
class IDeckLinkInput;
class IDeckLinkVideoInputFrame;
class DeckLinkInputCallback;

class LLive : public LObject {
    friend class DeckLinkInputCallback;
public:
    LLive();
    ~LLive();

    // 1. Ana Cihaz Yönetimi (DeckLink, NDI Receiver, Test Signal)
    void DeviceGetCount(int& count);
    void DeviceGetByIndex(int index, std::string& name, std::string& desc);
    void DeviceSet(int index);

    // 2. Kanal / Giriş Yönetimi (SDI, HDMI, NDI Kaynakları)
    void DeviceChannelGetCount(int deviceIndex, int& count);
    void DeviceChannelGetByIndex(int deviceIndex, int channelIndex, std::string& name, std::string& desc);
    void DeviceChannelSet(int channelIndex);

    // 3. Donanım Format Yönetimi (BMD/NDI Kaynak Formatı)
    void DeviceFormatVideoGetCount(int deviceIndex, int channelIndex, int& count);
    void DeviceFormatVideoGetByIndex(int deviceIndex, int channelIndex, int formatIndex, std::string& name, videoFormatProps& props);
    void DeviceFormatVideoSet(int formatIndex);

    // Props ayarları
    void setProps(const std::string& key, const std::string& value);
    
    // Filtre Yönetimi
    void setVideoFilter(LFilter* filter);
    void setAudioFilter(LFilter* filter);
    
    // Yazılımsal Çıkış Formatı (Normalization)
    void setVideoFormat(const videoFormatProps& props);
    void getVideoFormat(videoFormatProps& props);
    void setAudioFormat(const audioFormatProps& props);
    void getAudioFormat(audioFormatProps& props);

    // 3. Kontrol
    bool Start();
    void Stop();

    // 4. Durum Bilgisi
    bool isRunning() const { return m_isRunning; }
    void statusGet(int& status) override;
    double getFPS() override { return (targetVideoProps.fps > 0) ? targetVideoProps.fps : (m_sourceFps > 0 ? m_sourceFps : 25.0); }
    int getWidth() override { return m_currentWidth; }
    int getHeight() override { return m_currentHeight; }
    std::string getModeName() override { return m_currentModeName; }
    std::string getTimeCode() override;

private:
    void setSourceFps(double fps) { m_sourceFps = fps; }
    void setModeName(const std::string& name) { m_currentModeName = name; }
    double m_sourceFps = 25.0;
    int m_currentWidth = 0;
    int m_currentHeight = 0;
    std::string m_currentModeName = "No Signal";
    uint32_t m_lastBmdModePtr = 0;
    int m_lastBmdPixelFormat = 0;
    bool m_isTestSignal = false;
    double m_tonePhase = 0;
    
    videoFormatProps targetVideoProps;
    audioFormatProps targetAudioProps;

    struct SwsCache {
        struct SwsContext* ctx = nullptr;
        int srcW = 0, srcH = 0;
        int srcFmt = -1;
        int dstW = 0, dstH = 0;
        int dstFmt = -1;
    } m_swsCache;
    
    struct SwrCache {
        struct SwrContext* ctx = nullptr;
        int srcRate = 0;
        int srcCh = 0;
        int srcFmt = -1;
        int dstRate = 0;
        int dstCh = 0;
        int dstFmt = -1;
    } m_swrCache;

    // DeckLink Nesneleri (forward declared - başlık gerekmez)
    IDeckLink* m_deckLink = nullptr;
    IDeckLinkInput* m_deckLinkInput = nullptr;
    DeckLinkInputCallback* m_deckLinkCallback = nullptr;

    // NDI Nesneleri — tipler void* olarak saklanır, .cpp'de cast edilir.
    // Bu sayede kullanıcının NDI başlık yoluna ihtiyacı kalmaz.
    void* m_ndiFinder  = nullptr;  // NDIlib_find_instance_t
    void* m_ndiReceiver = nullptr; // NDIlib_recv_instance_t
    void* m_pNDI_find  = nullptr;  // NDIlib_find_instance_t (kalıcı bulucu)

    // VTP Nesneleri — tipler void* olarak saklanır, .cpp'de cast edilir.
    void* m_vtpReceiver = nullptr; // vtp_receiver_t
    void* m_vtpListener = nullptr; // vtp_listener_t

    std::thread m_ndiThread;
    std::thread m_vtpThread;
    std::thread m_decklinkThread;
    std::thread m_testThread;
    std::deque<IDeckLinkVideoInputFrame*> m_dlQueue;
    std::mutex m_dlMutex;
    std::condition_variable m_dlCond;

    void ndiLoop();
    void vtpLoop();
    void decklinkLoop();
    void processVideoFrame(struct AVFrame* frame);
    void processAudioFrame(struct AVFrame* frame);

    std::atomic<bool> m_isRunning;
    
    bool m_timeCodeEnabled = false;
    int m_timeCodeSource = 0; // 0: System, 1: Source, 2: Relative (timecodeStart)
    std::string m_timeCodeStart = "01:00:00:00";
    std::string m_currentTimeCode = "00:00:00:00";
    std::mutex m_tcMutex;
    
    struct AVFrame* cachedNoSignalFrame = nullptr;

    int m_fpsCounter = 0;
    std::chrono::steady_clock::time_point m_lastFpsCheck;
    std::chrono::steady_clock::time_point m_startTime;
    
    // Dahili Listeler (Caching için)
    std::vector<IDeckLink*> m_deckLinkList;
    std::vector<std::string> m_ndiSourceList;
    std::vector<std::string> m_vtpSourceList;
    std::vector<std::string> m_videoLines; // SDI, HDMI vb.
    
    struct DisplayMode {
        std::string name;
        videoFormatProps props;
        void* modePtr; // IDeckLinkDisplayMode*
    };
    std::vector<DisplayMode> m_displayModes;

    int m_selectedDeviceIndex = -1;
    int m_selectedChannelIndex = -1;
    int m_selectedFormatIndex = -1;
    int m_audioGain = 100; // 0-100
    
    // Dahili İşleyiş
    void updateNDISources();
    void updateVTPSources();
    void updateDeckLinkLines(int deviceIndex);
    void updateDisplayModes(int deviceIndex, int channelIndex);
    void cleanup();

    bool m_gpuEnabled = false;
    LFilter* m_videoFilter = nullptr;
    LFilter* m_audioFilter = nullptr;

    // FFmpeg Filtre Grafiği Nesneleri
    struct AVFilterGraph *filter_graph = nullptr;
    struct AVFilterContext *buffersink_ctx = nullptr;
    struct AVFilterContext *buffersrc_ctx = nullptr;
    std::string currentFilterDesc = "";
    struct AVFrame* filterFrame = nullptr;

    struct AVFilterGraph *audioFilterGraph = nullptr;
    struct AVFilterContext *audioSrcCtx = nullptr;
    struct AVFilterContext *audioSinkCtx = nullptr;
    std::string m_currentAudioFilterDesc = "";
    struct AVFrame* audioFilterFrame = nullptr;

    bool initAudioFilters(const std::string& filterStr, int sampleRate, enum AVSampleFormat sampleFmt, const struct AVChannelLayout& chLayout);
    bool build_filters(const std::string& filterDesc, int srcW, int srcH, enum AVPixelFormat srcFmt, struct AVRational srcTimebase, struct AVRational srcSar);
    void processVideoFrameInternal(struct AVFrame* frame);
    void processAudioFrameInternal(struct AVFrame* frame);
};
