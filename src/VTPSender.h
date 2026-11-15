#pragma once

#include "VTPCommon.h"
#include <memory>
#include <string>

namespace vtp {

class VTPSender {
public:
  VTPSender(const std::string &stream_name, int width, int height, int fps,
            bool enable_alpha, bool enable_audio);
  explicit VTPSender(const vtp_sender_config_t &config);
  ~VTPSender();

  bool Start();
  void Stop();

  bool SendFrame(const unsigned char *data, VTPPixelFormat format,
                 uint64_t timestamp_ns);
  bool SendFrameEx(const unsigned char *data, int stride, int width, int height,
                   VTPPixelFormat format, uint64_t timestamp_ns,
                   const vtp_video_metadata_t *metadata);
  bool SendFramePlanar(const unsigned char *const *planes, const int *strides,
                       int width, int height, VTPPixelFormat format,
                       uint64_t timestamp_ns,
                       const vtp_video_metadata_t *metadata);

  bool SendAudio(const unsigned char *pcm_data, int sample_count,
                 uint32_t sample_rate, uint8_t channels, uint64_t timestamp_ns);
  bool SendAudioEx(const void *pcm_data, int sample_count, uint32_t sample_rate,
                   uint8_t channels, vtp_audio_format_t format,
                   uint64_t timestamp_ns);

  bool SendMetadata(const char *metadata_utf8, uint64_t timestamp_ns);

  void SetScaleQuality(int quality);
  void SetCallbacks(const vtp_sender_callbacks_t *callbacks, void *user_data);

  uint16_t GetPort() const;
  std::string GetStreamName() const;
  bool GetTally(vtp_tally_state_t *out_tally) const;
  uint32_t GetClientCount() const;
  bool GetStats(vtp_stats_t *out_stats) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace vtp
