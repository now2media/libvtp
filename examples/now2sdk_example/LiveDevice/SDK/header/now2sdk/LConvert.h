#pragma once
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include "LFormat.h"

class LConvert {
public:
  LConvert();
  ~LConvert();

  // 1. Ayarlar (LRecorder ile uyumlu)
  void setContainer(const std::string &container);
  void setVideoCodec(const std::string &codec);
  void setAudioCodec(const std::string &codec);
  void setVideoBitrate(int bitrateKbps);
  void setAudioBitrate(int bitrateKbps);
  void setVideoFormat(const videoFormatProps &props);
  void setAudioFormat(const audioFormatProps &props);
  void setFilePath(const std::string &filePath);
  void setProps(const std::string &key, const std::string &value);

  // 2. Dosya Listesi Yönetimi
  void addInputFile(const std::string &filePath);
  void removeInputFile(const std::string &filePath);
  void clearInputFiles();

  // 3. Kontrol İşlevleri
  bool Start();
  void Stop();
  void statusGet(int &status);
  double getProgress();

  // 4. Dinamik Sorgulama (UI İçin)
  static std::vector<std::string> getAvailableContainers();
  static std::vector<std::string>
  getAvailableVideoCodecs(const std::string &container);
  static std::vector<std::string>
  getAvailableAudioCodecs(const std::string &container);

private:
  void convertLoop();
  bool initFFmpeg();
  AVFrame *allocVideoFrame(int width, int height, AVPixelFormat format);
  AVFrame *allocAudioFrame(int channels, int sampleRate, AVSampleFormat format,
                           int nb_samples);

  // Ayarlar
  std::string m_container = "mp4";
  std::string m_videoCodec = "libx264";
  std::string m_audioCodec = "aac";
  int m_videoBitrateKbps = 15000;
  int m_audioBitrateKbps = 128;
  std::string m_filePath;
  videoFormatProps m_videoProps;
  audioFormatProps m_audioProps;
  std::map<std::string, std::string> m_customOptions;

  // Dosya listesi ve durum
  std::vector<std::string> m_inputFiles;
  std::thread m_convertThread;
  std::atomic<bool> m_isConverting{false};
  std::atomic<bool> m_stopRequested{false};
  std::atomic<double> m_progress{0.0};
  std::atomic<int> m_status{0}; // 0: Idle/Finished, 1: Converting, -1: Error

  // FFmpeg Çıkış/Muxer nesneleri
  AVFormatContext *m_fmtCtx = nullptr;
  AVCodecContext *m_vCodecCtx = nullptr;
  AVCodecContext *m_aCodecCtx = nullptr;
  AVStream *m_vStream = nullptr;
  AVStream *m_aStream = nullptr;

  AVDictionary *m_vOptions = nullptr;
  AVDictionary *m_aOptions = nullptr;
};
