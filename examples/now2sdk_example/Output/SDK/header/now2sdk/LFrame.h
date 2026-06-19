#ifndef LFRAME_H
#define LFRAME_H

#include "LSink.h"
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

class LObject;

class LFrame : public LSink {
public:
    // Kullanıcının alacağı Video verisi yapısı
    struct Video {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> data; // RGBA Piksel verisi
        int linesize = 0;          // Bir satırın byte uzunluğu (width * 4)
        std::string timecode = "00:00:00:00";
        
        bool isValid() const { return !data.empty() && width > 0 && height > 0; }
    };

    // Kullanıcının alacağı Ses verisi yapısı
    struct Audio {
        int channels = 0;
        int sampleRate = 0;
        std::vector<uint8_t> data; // PCM Ses verisi
        int nb_samples = 0;        // Örnek sayısı
        
        bool isValid() const { return !data.empty() && nb_samples > 0; }
    };

    LFrame();
    virtual ~LFrame();

    // Kaynağa bağlanma fonksiyonu
    void readerObject(LObject* source);

    // Kullanıcının çağıracağı fonksiyonlar
    Video getVideo();
    Audio getAudio();

    // LSink arayüzünden gelen fonksiyonlar (Arka planda çalışır)
    void pushVideoFrame(AVFrame* frame) override;
    void pushAudioFrame(AVFrame* frame) override;

private:
    std::mutex m_mutex;
    Video m_latestVideo;
    Audio m_latestAudio;
    
    LObject* m_source = nullptr;
    
    // Video dönüştürücü (YUV -> RGBA)
    struct SwsContext* m_swsCtx = nullptr;
    
    // Ses dönüştürücü (Herhangi bir formattan -> S16 PCM)
    struct SwrContext* m_swrCtx = nullptr;
    uint8_t* m_audioResampleBuf = nullptr;
    int m_audioBufSize = 192000;
};

#endif // LFRAME_H
