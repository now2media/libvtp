#pragma once

#include "VTPCommon.h"
#include "VTPDiscovery.h"
#include <memory>
#include <string>

namespace vtp {

class VTPReceiver {
public:
  VTPReceiver();
  explicit VTPReceiver(const vtp_receiver_config_t &config);
  ~VTPReceiver();

  bool Connect(const std::string &ip, uint16_t port);
  bool ConnectEx(const std::string &ip, uint16_t port, int timeout_ms);
  bool ConnectByName(VTPDiscoveryListener *listener, const std::string &name);
  bool SetAutoReconnectName(VTPDiscoveryListener *listener,
                            const std::string &name, bool enable);
  void Disconnect();

  bool ReceiveFrame(VTPFrame *out_frame);
  bool ReceiveFrameEx(vtp_frame_ex_t *out_frame, int timeout_ms);

  bool ReceiveAudio(VTPAudioFrame *out_audio);
  bool ReceiveAudioEx(vtp_audio_frame_ex_t *out_audio, int timeout_ms);

  bool ReceiveMetadata(char *out_buf, size_t buf_size, uint64_t *out_ts_ns,
                       int timeout_ms);

  bool SetTally(const vtp_tally_state_t *tally);
  bool SendPTZ(const vtp_ptz_command_t *cmd);
  bool SendControl(const char *command_utf8);
  bool SetStreamTier(vtp_stream_tier_t tier);

  void SetCallbacks(const vtp_receiver_callbacks_t *callbacks, void *user_data);

  bool IsConnected() const;
  bool GetStats(vtp_stats_t *out_stats) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace vtp
