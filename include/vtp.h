#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef VTP_EXPORTS
#define VTP_API __declspec(dllexport)
#else
#define VTP_API __declspec(dllimport)
#endif
#else
#define VTP_API __attribute__((visibility("default")))
#endif

#define VTP_MAX_SOURCES 64

typedef enum {
  VTP_FORMAT_RGB = 0,
  VTP_FORMAT_RGBA = 1,
  VTP_FORMAT_BGR = 2,
  VTP_FORMAT_BGRA = 3,
  VTP_FORMAT_I420 = 4,
  VTP_FORMAT_YV12 = 5,
  VTP_FORMAT_NV12 = 6,
  VTP_FORMAT_UYVY = 7,
  VTP_FORMAT_YUY2 = 8,
  VTP_FORMAT_I422 = 9
} vtp_pixel_format_t;

typedef enum {
  VTP_LOG_DEBUG = 0,
  VTP_LOG_INFO = 1,
  VTP_LOG_WARN = 2,
  VTP_LOG_ERROR = 3,
  VTP_LOG_NONE = 4
} vtp_log_level_t;

typedef enum {
  VTP_TRANSPORT_TCP = 0,
  VTP_TRANSPORT_UDP = 1
} vtp_transport_t;

typedef enum {
  VTP_TIER_FULL = 0,
  VTP_TIER_PROXY = 1
} vtp_stream_tier_t;

typedef enum {
  VTP_AUDIO_PCM_S16LE = 0,
  VTP_AUDIO_PCM_S24LE = 1,
  VTP_AUDIO_PCM_S32LE = 2,
  VTP_AUDIO_PCM_F32LE = 3,
  VTP_AUDIO_PCM_F32LE_PLANAR = 4
} vtp_audio_format_t;

typedef enum {
  VTP_AUDIO_CODEC_PCM = 0,
  VTP_AUDIO_CODEC_OPUS = 1
} vtp_audio_codec_t;

typedef enum {
  VTP_COLOR_BT709 = 0,
  VTP_COLOR_BT601 = 1,
  VTP_COLOR_BT2020 = 2,
  VTP_COLOR_SRGB = 3
} vtp_color_space_t;

typedef enum {
  VTP_SCAN_PROGRESSIVE = 0,
  VTP_SCAN_INTERLACED_TFF = 1,
  VTP_SCAN_INTERLACED_BFF = 2
} vtp_scan_mode_t;

typedef enum {
  VTP_PTZ_MOVE_SPEED = 0,
  VTP_PTZ_SET_ABSOLUTE = 1,
  VTP_PTZ_STORE_PRESET = 2,
  VTP_PTZ_RECALL_PRESET = 3,
  VTP_PTZ_AUTOFOCUS = 4
} vtp_ptz_action_t;

typedef struct {
  uint32_t magic;
  uint32_t frame_index;
  uint32_t width;
  uint32_t height;
  uint8_t pixel_format;
  uint8_t has_alpha;
  uint16_t reserved;
  uint32_t fill_size;
  uint32_t key_size;
  uint64_t timestamp_ns;
} vtp_frame_header_t;

typedef struct {
  char name[128];
  char ip[64];
  uint16_t port;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  bool has_alpha;
  bool has_audio;
} vtp_source_t;

typedef struct {
  const unsigned char *data;
  uint32_t width;
  uint32_t height;
  vtp_pixel_format_t format;
  uint64_t timestamp_ns;
  uint32_t frame_index;
  bool has_alpha;
} vtp_frame_t;

typedef struct {
  const unsigned char *data;
  uint32_t sample_rate;
  uint8_t channels;
  uint32_t sample_count;
  uint32_t data_size;
  uint64_t timestamp_ns;
} vtp_audio_frame_t;

typedef struct {
  bool on_program;
  bool on_preview;
} vtp_tally_state_t;

typedef struct {
  float pan;
  float tilt;
  float zoom;
  float focus;
  uint32_t preset_id;
  uint8_t action;
} vtp_ptz_command_t;

typedef struct {
  uint64_t timecode_ns;
  vtp_color_space_t color_space;
  vtp_scan_mode_t scan_mode;
  float aspect_ratio;
  bool full_range;
} vtp_video_metadata_t;

typedef struct {
  const unsigned char *data;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  vtp_pixel_format_t format;
  uint64_t timestamp_ns;
  uint32_t frame_index;
  bool has_alpha;
  vtp_video_metadata_t metadata;
} vtp_frame_ex_t;

typedef struct {
  const unsigned char *data;
  uint32_t sample_rate;
  uint8_t channels;
  vtp_audio_format_t format;
  uint32_t sample_count;
  uint32_t data_size;
  uint64_t timestamp_ns;
} vtp_audio_frame_ex_t;

typedef struct {
  double bitrate_mbps;
  double fps;
  uint64_t bytes_transferred;
  uint64_t frames_processed;
  uint64_t frames_dropped;
  uint64_t audio_packets_processed;
  uint32_t last_codec_time_us;
  uint32_t connected_clients;
  uint32_t audio_queue_depth;
  double rtt_ms;
} vtp_stats_t;

typedef struct {
  const char *stream_name;
  const char *bind_ip;
  uint16_t port;
  int width;
  int height;
  int fps;
  bool enable_alpha;
  bool enable_audio;
  vtp_transport_t transport;
  int quality;
  int slice_threads;
  bool enable_proxy;
  int proxy_quality;
  vtp_audio_codec_t audio_codec;
  int opus_bitrate;
  bool enable_fec;
} vtp_sender_config_t;

typedef struct {
  int connect_timeout_ms;
  bool auto_reconnect;
  int reconnect_interval_ms;
  int preferred_output_format;
  vtp_stream_tier_t preferred_tier;
  vtp_transport_t transport;
} vtp_receiver_config_t;

typedef void (*vtp_log_callback_t)(vtp_log_level_t level, const char *msg,
                                   void *user_data);

typedef struct {
  void (*on_tally)(const vtp_tally_state_t *tally, void *user_data);
  void (*on_ptz)(const vtp_ptz_command_t *cmd, void *user_data);
  void (*on_control)(const char *cmd, void *user_data);
  void (*on_client_count)(uint32_t count, void *user_data);
} vtp_sender_callbacks_t;

typedef struct {
  void (*on_frame)(const vtp_frame_ex_t *frame, void *user_data);
  void (*on_audio)(const vtp_audio_frame_ex_t *audio, void *user_data);
  void (*on_metadata)(const char *meta, uint64_t ts_ns, void *user_data);
  void (*on_connection_state)(bool connected, void *user_data);
} vtp_receiver_callbacks_t;

typedef struct vtp_sender vtp_sender_t;
typedef struct vtp_receiver vtp_receiver_t;
typedef struct vtp_listener vtp_listener_t;

// LogAPI
VTP_API void vtp_set_log_callback(vtp_log_callback_t cb, void *user_data);
VTP_API void vtp_set_log_level(vtp_log_level_t level);

// SenderAPI
VTP_API void vtp_sender_config_init(vtp_sender_config_t *config);
VTP_API vtp_sender_t *vtp_create_sender(const char *stream_name, int width,
                                        int height, int fps, bool enable_alpha,
                                        bool enable_audio);
VTP_API vtp_sender_t *vtp_create_sender_ex(const vtp_sender_config_t *config);
VTP_API void vtp_destroy_sender(vtp_sender_t *sender);
VTP_API bool vtp_sender_start(vtp_sender_t *sender);
VTP_API void vtp_sender_stop(vtp_sender_t *sender);
VTP_API bool vtp_sender_send_frame(vtp_sender_t *sender,
                                   const unsigned char *data,
                                   vtp_pixel_format_t format,
                                   uint64_t timestamp_ns);
VTP_API bool vtp_sender_send_frame_ex(vtp_sender_t *sender,
                                      const unsigned char *data, int stride,
                                      int width, int height,
                                      vtp_pixel_format_t format,
                                      uint64_t timestamp_ns,
                                      const vtp_video_metadata_t *metadata);
VTP_API bool vtp_sender_send_frame_planar(vtp_sender_t *sender,
                                          const unsigned char *const *planes,
                                          const int *strides, int width,
                                          int height, vtp_pixel_format_t format,
                                          uint64_t timestamp_ns,
                                          const vtp_video_metadata_t *metadata);
VTP_API bool vtp_sender_send_audio(vtp_sender_t *sender,
                                   const unsigned char *pcm_data,
                                   int sample_count, uint32_t sample_rate,
                                   uint8_t channels, uint64_t timestamp_ns);
VTP_API bool vtp_sender_send_audio_ex(vtp_sender_t *sender,
                                      const void *pcm_data, int sample_count,
                                      uint32_t sample_rate, uint8_t channels,
                                      vtp_audio_format_t format,
                                      uint64_t timestamp_ns);
VTP_API bool vtp_sender_send_metadata(vtp_sender_t *sender,
                                      const char *metadata_utf8,
                                      uint64_t timestamp_ns);
VTP_API uint16_t vtp_sender_get_port(vtp_sender_t *sender);
VTP_API void vtp_sender_set_scale_quality(vtp_sender_t *sender, int quality);
VTP_API bool vtp_sender_get_tally(vtp_sender_t *sender,
                                  vtp_tally_state_t *out_tally);
VTP_API void vtp_sender_set_callbacks(vtp_sender_t *sender,
                                      const vtp_sender_callbacks_t *callbacks,
                                      void *user_data);
VTP_API uint32_t vtp_sender_get_client_count(vtp_sender_t *sender);
VTP_API bool vtp_sender_get_stats(vtp_sender_t *sender,
                                  vtp_stats_t *out_stats);

// ReceiverAPI
VTP_API void vtp_receiver_config_init(vtp_receiver_config_t *config);
VTP_API vtp_receiver_t *vtp_create_receiver();
VTP_API vtp_receiver_t *
vtp_create_receiver_ex(const vtp_receiver_config_t *config);
VTP_API void vtp_destroy_receiver(vtp_receiver_t *receiver);
VTP_API bool vtp_receiver_connect(vtp_receiver_t *receiver, const char *ip,
                                  uint16_t port);
VTP_API bool vtp_receiver_connect_ex(vtp_receiver_t *receiver, const char *ip,
                                     uint16_t port, int timeout_ms);
VTP_API bool vtp_receiver_connect_by_name(vtp_receiver_t *receiver,
                                          vtp_listener_t *listener,
                                          const char *name);
VTP_API bool vtp_receiver_set_auto_reconnect_name(vtp_receiver_t *receiver,
                                                  vtp_listener_t *listener,
                                                  const char *stream_name,
                                                  bool enable);
VTP_API void vtp_receiver_disconnect(vtp_receiver_t *receiver);
VTP_API bool vtp_receiver_receive_frame(vtp_receiver_t *receiver,
                                        vtp_frame_t *out_frame);
VTP_API bool vtp_receiver_receive_frame_ex(vtp_receiver_t *receiver,
                                           vtp_frame_ex_t *out_frame,
                                           int timeout_ms);
VTP_API bool vtp_receiver_receive_audio(vtp_receiver_t *receiver,
                                        vtp_audio_frame_t *out_audio);
VTP_API bool vtp_receiver_receive_audio_ex(vtp_receiver_t *receiver,
                                           vtp_audio_frame_ex_t *out_audio,
                                           int timeout_ms);
VTP_API bool vtp_receiver_receive_metadata(vtp_receiver_t *receiver,
                                           char *out_buf, size_t buf_size,
                                           uint64_t *out_timestamp_ns,
                                           int timeout_ms);
VTP_API bool vtp_receiver_is_connected(vtp_receiver_t *receiver);
VTP_API bool vtp_receiver_set_tally(vtp_receiver_t *receiver,
                                    const vtp_tally_state_t *tally);
VTP_API bool vtp_receiver_send_ptz(vtp_receiver_t *receiver,
                                   const vtp_ptz_command_t *cmd);
VTP_API bool vtp_receiver_send_control(vtp_receiver_t *receiver,
                                       const char *command_utf8);
VTP_API bool vtp_receiver_set_stream_tier(vtp_receiver_t *receiver,
                                          vtp_stream_tier_t tier);
VTP_API void
vtp_receiver_set_callbacks(vtp_receiver_t *receiver,
                           const vtp_receiver_callbacks_t *callbacks,
                           void *user_data);
VTP_API bool vtp_receiver_get_stats(vtp_receiver_t *receiver,
                                    vtp_stats_t *out_stats);

// ListenerAPI
VTP_API vtp_listener_t *vtp_create_listener();
VTP_API void vtp_destroy_listener(vtp_listener_t *listener);
VTP_API void vtp_listener_start(vtp_listener_t *listener);
VTP_API void vtp_listener_stop(vtp_listener_t *listener);
VTP_API int vtp_listener_get_sources(vtp_listener_t *listener,
                                     vtp_source_t *out_sources,
                                     int max_sources);

// TestPatternAPI
VTP_API bool vtp_generate_test_pattern(unsigned char *buffer, int width,
                                       int height, int stride,
                                       vtp_pixel_format_t format,
                                       uint32_t frame_index);
VTP_API bool vtp_generate_test_tone(int16_t *pcm_buffer, int sample_count,
                                    uint32_t sample_rate, uint8_t channels,
                                    double frequency_hz,
                                    uint64_t *sample_phase_inout);

#ifdef __cplusplus
}
#endif
