#include "vtp.h"
#include "VTPDiscovery.h"
#include "VTPLog.h"
#include "VTPReceiver.h"
#include "VTPSender.h"
#include "VTPTestPattern.h"
#include <cstring>

using namespace vtp;

extern "C" {

// LogAPI

void vtp_set_log_callback(vtp_log_callback_t cb, void *user_data) {
  SetLogCallback(cb, user_data);
}

void vtp_set_log_level(vtp_log_level_t level) { SetLogLevel(level); }

// SenderAPI

void vtp_sender_config_init(vtp_sender_config_t *config) {
  if (!config)
    return;
  std::memset(config, 0, sizeof(*config));
  config->stream_name = "VTP-Stream";
  config->bind_ip = "0.0.0.0";
  config->port = 0;
  config->width = 1920;
  config->height = 1080;
  config->fps = 30;
  config->enable_alpha = false;
  config->enable_audio = true;
  config->transport = VTP_TRANSPORT_TCP;
  config->quality = 90;
  config->slice_threads = 0;
  config->enable_proxy = false;
  config->proxy_quality = 65;
  config->audio_codec = VTP_AUDIO_CODEC_PCM;
  config->opus_bitrate = 128000;
  config->enable_fec = false;
}

vtp_sender_t *vtp_create_sender(const char *stream_name, int width, int height,
                                int fps, bool enable_alpha, bool enable_audio) {
  const char *name = stream_name ? stream_name : "VTP-Stream";
  return reinterpret_cast<vtp_sender_t *>(new VTPSender(
      name, width, height, fps, enable_alpha, enable_audio));
}

vtp_sender_t *vtp_create_sender_ex(const vtp_sender_config_t *config) {
  if (!config) {
    vtp_sender_config_t def{};
    vtp_sender_config_init(&def);
    return reinterpret_cast<vtp_sender_t *>(new VTPSender(def));
  }
  return reinterpret_cast<vtp_sender_t *>(new VTPSender(*config));
}

void vtp_destroy_sender(vtp_sender_t *sender) {
  if (sender) {
    delete reinterpret_cast<VTPSender *>(sender);
  }
}

bool vtp_sender_start(vtp_sender_t *sender) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->Start();
}

void vtp_sender_stop(vtp_sender_t *sender) {
  if (sender) {
    reinterpret_cast<VTPSender *>(sender)->Stop();
  }
}

bool vtp_sender_send_frame(vtp_sender_t *sender, const unsigned char *data,
                           vtp_pixel_format_t format, uint64_t timestamp_ns) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendFrame(
      data, static_cast<VTPPixelFormat>(format), timestamp_ns);
}

bool vtp_sender_send_frame_ex(vtp_sender_t *sender, const unsigned char *data,
                              int stride, int width, int height,
                              vtp_pixel_format_t format, uint64_t timestamp_ns,
                              const vtp_video_metadata_t *metadata) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendFrameEx(
      data, stride, width, height, format, timestamp_ns, metadata);
}

bool vtp_sender_send_frame_planar(vtp_sender_t *sender,
                                  const unsigned char *const *planes,
                                  const int *strides, int width, int height,
                                  vtp_pixel_format_t format,
                                  uint64_t timestamp_ns,
                                  const vtp_video_metadata_t *metadata) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendFramePlanar(
      planes, strides, width, height, format, timestamp_ns, metadata);
}

bool vtp_sender_send_audio(vtp_sender_t *sender, const unsigned char *pcm_data,
                           int sample_count, uint32_t sample_rate,
                           uint8_t channels, uint64_t timestamp_ns) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendAudio(
      pcm_data, sample_count, sample_rate, channels, timestamp_ns);
}

bool vtp_sender_send_audio_ex(vtp_sender_t *sender, const void *pcm_data,
                              int sample_count, uint32_t sample_rate,
                              uint8_t channels, vtp_audio_format_t format,
                              uint64_t timestamp_ns) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendAudioEx(
      pcm_data, sample_count, sample_rate, channels, format, timestamp_ns);
}

bool vtp_sender_send_metadata(vtp_sender_t *sender, const char *metadata_utf8,
                              uint64_t timestamp_ns) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->SendMetadata(metadata_utf8,
                                                             timestamp_ns);
}

uint16_t vtp_sender_get_port(vtp_sender_t *sender) {
  if (!sender)
    return 0;
  return reinterpret_cast<VTPSender *>(sender)->GetPort();
}

void vtp_sender_set_scale_quality(vtp_sender_t *sender, int quality) {
  if (sender) {
    reinterpret_cast<VTPSender *>(sender)->SetScaleQuality(quality);
  }
}

bool vtp_sender_get_tally(vtp_sender_t *sender, vtp_tally_state_t *out_tally) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->GetTally(out_tally);
}

void vtp_sender_set_callbacks(vtp_sender_t *sender,
                              const vtp_sender_callbacks_t *callbacks,
                              void *user_data) {
  if (sender) {
    reinterpret_cast<VTPSender *>(sender)->SetCallbacks(callbacks, user_data);
  }
}

uint32_t vtp_sender_get_client_count(vtp_sender_t *sender) {
  if (!sender)
    return 0;
  return reinterpret_cast<VTPSender *>(sender)->GetClientCount();
}

bool vtp_sender_get_stats(vtp_sender_t *sender, vtp_stats_t *out_stats) {
  if (!sender)
    return false;
  return reinterpret_cast<VTPSender *>(sender)->GetStats(out_stats);
}

// ReceiverAPI

void vtp_receiver_config_init(vtp_receiver_config_t *config) {
  if (!config)
    return;
  std::memset(config, 0, sizeof(*config));
  config->connect_timeout_ms = 2000;
  config->auto_reconnect = false;
  config->reconnect_interval_ms = 1000;
  config->preferred_output_format = -1;
  config->preferred_tier = VTP_TIER_FULL;
  config->transport = VTP_TRANSPORT_TCP;
}

vtp_receiver_t *vtp_create_receiver() {
  return reinterpret_cast<vtp_receiver_t *>(new VTPReceiver());
}

vtp_receiver_t *vtp_create_receiver_ex(const vtp_receiver_config_t *config) {
  if (!config) {
    vtp_receiver_config_t def{};
    vtp_receiver_config_init(&def);
    return reinterpret_cast<vtp_receiver_t *>(new VTPReceiver(def));
  }
  return reinterpret_cast<vtp_receiver_t *>(new VTPReceiver(*config));
}

void vtp_destroy_receiver(vtp_receiver_t *receiver) {
  if (receiver) {
    delete reinterpret_cast<VTPReceiver *>(receiver);
  }
}

bool vtp_receiver_connect(vtp_receiver_t *receiver, const char *ip,
                          uint16_t port) {
  if (!receiver || !ip)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->Connect(ip, port);
}

bool vtp_receiver_connect_ex(vtp_receiver_t *receiver, const char *ip,
                             uint16_t port, int timeout_ms) {
  if (!receiver || !ip)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ConnectEx(ip, port,
                                                              timeout_ms);
}

bool vtp_receiver_connect_by_name(vtp_receiver_t *receiver,
                                  vtp_listener_t *listener, const char *name) {
  if (!receiver || !listener || !name)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ConnectByName(
      reinterpret_cast<VTPDiscoveryListener *>(listener), name);
}

bool vtp_receiver_set_auto_reconnect_name(vtp_receiver_t *receiver,
                                          vtp_listener_t *listener,
                                          const char *stream_name,
                                          bool enable) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->SetAutoReconnectName(
      reinterpret_cast<VTPDiscoveryListener *>(listener),
      stream_name ? stream_name : "", enable);
}

void vtp_receiver_disconnect(vtp_receiver_t *receiver) {
  if (receiver) {
    reinterpret_cast<VTPReceiver *>(receiver)->Disconnect();
  }
}

bool vtp_receiver_receive_frame(vtp_receiver_t *receiver,
                                vtp_frame_t *out_frame) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ReceiveFrame(out_frame);
}

bool vtp_receiver_receive_frame_ex(vtp_receiver_t *receiver,
                                   vtp_frame_ex_t *out_frame, int timeout_ms) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ReceiveFrameEx(out_frame,
                                                                   timeout_ms);
}

bool vtp_receiver_receive_audio(vtp_receiver_t *receiver,
                                vtp_audio_frame_t *out_audio) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ReceiveAudio(
      reinterpret_cast<VTPAudioFrame *>(out_audio));
}

bool vtp_receiver_receive_audio_ex(vtp_receiver_t *receiver,
                                   vtp_audio_frame_ex_t *out_audio,
                                   int timeout_ms) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ReceiveAudioEx(out_audio,
                                                                   timeout_ms);
}

bool vtp_receiver_receive_metadata(vtp_receiver_t *receiver, char *out_buf,
                                   size_t buf_size, uint64_t *out_timestamp_ns,
                                   int timeout_ms) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->ReceiveMetadata(
      out_buf, buf_size, out_timestamp_ns, timeout_ms);
}

bool vtp_receiver_is_connected(vtp_receiver_t *receiver) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->IsConnected();
}

bool vtp_receiver_set_tally(vtp_receiver_t *receiver,
                            const vtp_tally_state_t *tally) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->SetTally(tally);
}

bool vtp_receiver_send_ptz(vtp_receiver_t *receiver,
                           const vtp_ptz_command_t *cmd) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->SendPTZ(cmd);
}

bool vtp_receiver_send_control(vtp_receiver_t *receiver,
                               const char *command_utf8) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->SendControl(command_utf8);
}

bool vtp_receiver_set_stream_tier(vtp_receiver_t *receiver,
                                  vtp_stream_tier_t tier) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->SetStreamTier(tier);
}

void vtp_receiver_set_callbacks(vtp_receiver_t *receiver,
                                const vtp_receiver_callbacks_t *callbacks,
                                void *user_data) {
  if (receiver) {
    reinterpret_cast<VTPReceiver *>(receiver)->SetCallbacks(callbacks,
                                                            user_data);
  }
}

bool vtp_receiver_get_stats(vtp_receiver_t *receiver, vtp_stats_t *out_stats) {
  if (!receiver)
    return false;
  return reinterpret_cast<VTPReceiver *>(receiver)->GetStats(out_stats);
}

// ListenerAPI

vtp_listener_t *vtp_create_listener() {
  return reinterpret_cast<vtp_listener_t *>(new VTPDiscoveryListener());
}

void vtp_destroy_listener(vtp_listener_t *listener) {
  if (listener) {
    delete reinterpret_cast<VTPDiscoveryListener *>(listener);
  }
}

void vtp_listener_start(vtp_listener_t *listener) {
  if (listener) {
    reinterpret_cast<VTPDiscoveryListener *>(listener)->Start();
  }
}

void vtp_listener_stop(vtp_listener_t *listener) {
  if (listener) {
    reinterpret_cast<VTPDiscoveryListener *>(listener)->Stop();
  }
}

int vtp_listener_get_sources(vtp_listener_t *listener,
                             vtp_source_t *out_sources, int max_sources) {
  if (!listener)
    return 0;
  return reinterpret_cast<VTPDiscoveryListener *>(listener)->GetSources(
      reinterpret_cast<VTPSource *>(out_sources), max_sources);
}

// TestPatternAPI

bool vtp_generate_test_pattern(unsigned char *buffer, int width, int height,
                               int stride, vtp_pixel_format_t format,
                               uint32_t frame_index) {
  return GenerateTestPattern(buffer, width, height, stride, format,
                             frame_index);
}

bool vtp_generate_test_tone(int16_t *pcm_buffer, int sample_count,
                            uint32_t sample_rate, uint8_t channels,
                            double frequency_hz, uint64_t *sample_phase_inout) {
  return GenerateTestTone(pcm_buffer, sample_count, sample_rate, channels,
                          frequency_hz, sample_phase_inout);
}

}
