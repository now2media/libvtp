#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Dışa açılacak fonksiyonları işaretlemek için API makrosu
#ifdef _WIN32
  #define VTP_API __declspec(dllexport)
#else
  #define VTP_API __attribute__((visibility("default")))
#endif

#define VTP_MAX_SOURCES 64

// Pixel format matching VTPCommon.h
typedef enum {
    VTP_FORMAT_RGB  = 0,
    VTP_FORMAT_RGBA = 1,
    VTP_FORMAT_BGR  = 2,
    VTP_FORMAT_BGRA = 3
} vtp_pixel_format_t;

// Frame metadata header matching VTPCommon.h
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

// Source representation matching VTPCommon.h
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

// Frame struct managed internally by VTP SDK
typedef struct {
    const unsigned char* data; // Pointer to SDK-allocated raw buffer
    uint32_t width;
    uint32_t height;
    vtp_pixel_format_t format;
    uint64_t timestamp_ns;
    uint32_t frame_index;
    bool has_alpha;
} vtp_frame_t;

// Audio Frame struct managed internally by VTP SDK
typedef struct {
    const unsigned char* data; // Pointer to SDK-allocated PCM buffer (Signed 16-bit integer PCM)
    uint32_t sample_rate;
    uint8_t channels;
    uint32_t sample_count;
    uint32_t data_size;
    uint64_t timestamp_ns;
} vtp_audio_frame_t;

// Handles
typedef struct vtp_sender vtp_sender_t;
typedef struct vtp_receiver vtp_receiver_t;
typedef struct vtp_listener vtp_listener_t;

// ==========================================
// Sender APIs
// ==========================================
VTP_API vtp_sender_t* vtp_create_sender(const char* stream_name, int width, int height, int fps, bool enable_alpha, bool enable_audio);
VTP_API void vtp_destroy_sender(vtp_sender_t* sender);
VTP_API bool vtp_sender_start(vtp_sender_t* sender);
VTP_API void vtp_sender_stop(vtp_sender_t* sender);
VTP_API bool vtp_sender_send_frame(vtp_sender_t* sender, const unsigned char* data, vtp_pixel_format_t format, uint64_t timestamp_ns);
VTP_API bool vtp_sender_send_audio(vtp_sender_t* sender, const unsigned char* pcm_data, int sample_count, uint32_t sample_rate, uint8_t channels, uint64_t timestamp_ns);
VTP_API uint16_t vtp_sender_get_port(vtp_sender_t* sender);
VTP_API void vtp_sender_set_scale_quality(vtp_sender_t* sender, int quality);

// ==========================================
// Receiver APIs
// ==========================================
VTP_API vtp_receiver_t* vtp_create_receiver();
VTP_API void vtp_destroy_receiver(vtp_receiver_t* receiver);
VTP_API bool vtp_receiver_connect(vtp_receiver_t* receiver, const char* ip, uint16_t port);
VTP_API bool vtp_receiver_connect_by_name(vtp_receiver_t* receiver, vtp_listener_t* listener, const char* name);
VTP_API void vtp_receiver_disconnect(vtp_receiver_t* receiver);
VTP_API bool vtp_receiver_receive_frame(vtp_receiver_t* receiver, vtp_frame_t* out_frame);
VTP_API bool vtp_receiver_receive_audio(vtp_receiver_t* receiver, vtp_audio_frame_t* out_audio);
VTP_API bool vtp_receiver_is_connected(vtp_receiver_t* receiver);

// ==========================================
// Discovery Listener APIs
// ==========================================
VTP_API vtp_listener_t* vtp_create_listener();
VTP_API void vtp_destroy_listener(vtp_listener_t* listener);
VTP_API void vtp_listener_start(vtp_listener_t* listener);
VTP_API void vtp_listener_stop(vtp_listener_t* listener);
VTP_API int vtp_listener_get_sources(vtp_listener_t* listener, vtp_source_t* out_sources, int max_sources);

#ifdef __cplusplus
}
#endif
