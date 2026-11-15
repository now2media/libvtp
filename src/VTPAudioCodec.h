#pragma once

#include "VTPCommon.h"
#include <cstdint>
#include <vector>

namespace vtp {
namespace audio {

size_t BytesPerSample(vtp_audio_format_t fmt);
size_t AudioPayloadSize(vtp_audio_format_t fmt, int sample_count, int channels);

void ConvertToS16LE(const void *src, vtp_audio_format_t src_fmt,
                    int sample_count, int channels, std::vector<int16_t> &dst);

void ConvertToFloatInterleaved(const void *src, vtp_audio_format_t src_fmt,
                               int sample_count, int channels,
                               std::vector<float> &dst);

struct EncodedOpusPacket {
  std::vector<uint8_t> data;
  uint16_t sample_count = 0;
  uint64_t timestamp_ns = 0;
};

class VTPOpusEncoder {
public:
  VTPOpusEncoder();
  ~VTPOpusEncoder();

  bool IsAvailable() const;
  bool Init(uint32_t sample_rate, uint8_t channels, int bitrate);
  void Reset();

  bool Encode(const void *src, vtp_audio_format_t src_fmt, int sample_count,
              uint32_t sample_rate, uint8_t channels, uint64_t timestamp_ns,
              std::vector<EncodedOpusPacket> &out_packets);

  uint32_t SampleRate() const { return sample_rate_; }
  uint8_t Channels() const { return channels_; }

private:
  struct Impl;
  Impl *impl_ = nullptr;
  uint32_t sample_rate_ = 0;
  uint8_t channels_ = 0;
  int bitrate_ = 128000;
  int frame_size_ = 0;
  std::vector<float> fifo_;
  std::vector<float> scratch_;
};

class VTPOpusDecoder {
public:
  VTPOpusDecoder();
  ~VTPOpusDecoder();

  bool IsAvailable() const;
  bool Init(uint32_t sample_rate, uint8_t channels);
  void Reset();

  int DecodeToFloat(const uint8_t *payload, int payload_size,
                    uint32_t sample_rate, uint8_t channels,
                    std::vector<float> &out_pcm);

private:
  struct Impl;
  Impl *impl_ = nullptr;
  uint32_t sample_rate_ = 0;
  uint8_t channels_ = 0;
};

}
} // namespace vtp
