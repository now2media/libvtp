#ifndef LREADER_H
#define LREADER_H

#include <string>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

class LReader {
public:
    LReader();
    ~LReader();

    // Ana işlem: Dosyayı analiz eder
    bool open(const std::string& filePath);
    void close();

    // Temel Bilgiler
    double getDurationMs();
    double getFps();
    int getWidth();
    int getHeight();
    long long getFileLen(); // Dosya boyutu (Bytes)

    // Codec ve Format Bilgileri
    std::string getVideoCodec();
    std::string getAudioCodec();
    std::string getContainer();
    long long getBitrate();
    long long getAudioBitrate();
    int getAudioChannels();

    // Profesyonel Metadata Özellikleri
    std::string getAspectRatio();
    std::string getColorSpace();
    std::string getChromaSubsampling();
    int getBitDepth();
    std::string getScanType();

    // In/Out Noktaları (Varsayılan 0 ve Duration)
    double getInPoint() { return 0; }
    double getOutPoint() { return getDurationMs(); }
    std::string getInTC();
    std::string getOutTC();

private:
    AVFormatContext* fmtCtx = nullptr;
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    std::string path = "";
};

#endif // LREADER_H
