#include "vtp.h"
#include "VTPSender.h"
#include "VTPReceiver.h"
#include "VTPDiscovery.h"
#include <cstring>

using namespace vtp;

extern "C" {

// ==========================================
// Sender APIs
// ==========================================

vtp_sender_t* vtp_create_sender(const char* stream_name, int width, int height, int fps, bool enable_alpha, bool enable_audio) {
    return reinterpret_cast<vtp_sender_t*>(new VTPSender(stream_name, width, height, fps, enable_alpha, enable_audio));
}

void vtp_destroy_sender(vtp_sender_t* sender) {
    if (sender) {
        delete reinterpret_cast<VTPSender*>(sender);
    }
}

bool vtp_sender_start(vtp_sender_t* sender) {
    if (!sender) return false;
    return reinterpret_cast<VTPSender*>(sender)->Start();
}

void vtp_sender_stop(vtp_sender_t* sender) {
    if (sender) {
        reinterpret_cast<VTPSender*>(sender)->Stop();
    }
}

bool vtp_sender_send_frame(vtp_sender_t* sender, const unsigned char* data, vtp_pixel_format_t format, uint64_t timestamp_ns) {
    if (!sender) return false;
    return reinterpret_cast<VTPSender*>(sender)->SendFrame(
        data, static_cast<VTPPixelFormat>(format), timestamp_ns);
}

bool vtp_sender_send_audio(vtp_sender_t* sender, const unsigned char* pcm_data, int sample_count, uint32_t sample_rate, uint8_t channels, uint64_t timestamp_ns) {
    if (!sender) return false;
    return reinterpret_cast<VTPSender*>(sender)->SendAudio(pcm_data, sample_count, sample_rate, channels, timestamp_ns);
}

uint16_t vtp_sender_get_port(vtp_sender_t* sender) {
    if (!sender) return 0;
    return reinterpret_cast<VTPSender*>(sender)->GetPort();
}

void vtp_sender_set_scale_quality(vtp_sender_t* sender, int quality) {
    if (sender) {
        reinterpret_cast<VTPSender*>(sender)->SetScaleQuality(quality);
    }
}

// ==========================================
// Receiver APIs
// ==========================================

vtp_receiver_t* vtp_create_receiver() {
    return reinterpret_cast<vtp_receiver_t*>(new VTPReceiver());
}

void vtp_destroy_receiver(vtp_receiver_t* receiver) {
    if (receiver) {
        delete reinterpret_cast<VTPReceiver*>(receiver);
    }
}

bool vtp_receiver_connect(vtp_receiver_t* receiver, const char* ip, uint16_t port) {
    if (!receiver) return false;
    return reinterpret_cast<VTPReceiver*>(receiver)->Connect(ip, port);
}

bool vtp_receiver_connect_by_name(vtp_receiver_t* receiver, vtp_listener_t* listener, const char* name) {
    if (!receiver || !listener || !name) return false;

    vtp_source_t sources[VTP_MAX_SOURCES];
    int count = vtp_listener_get_sources(listener, sources, VTP_MAX_SOURCES);

    for (int i = 0; i < count; ++i) {
        if (std::strcmp(sources[i].name, name) == 0) {
            return reinterpret_cast<VTPReceiver*>(receiver)->Connect(sources[i].ip, sources[i].port);
        }
    }
    return false;
}

void vtp_receiver_disconnect(vtp_receiver_t* receiver) {
    if (receiver) {
        reinterpret_cast<VTPReceiver*>(receiver)->Disconnect();
    }
}

bool vtp_receiver_receive_frame(vtp_receiver_t* receiver, vtp_frame_t* out_frame) {
    if (!receiver) return false;
    return reinterpret_cast<VTPReceiver*>(receiver)->ReceiveFrame(out_frame);
}

bool vtp_receiver_receive_audio(vtp_receiver_t* receiver, vtp_audio_frame_t* out_audio) {
    if (!receiver) return false;
    return reinterpret_cast<VTPReceiver*>(receiver)->ReceiveAudio(reinterpret_cast<VTPAudioFrame*>(out_audio));
}

bool vtp_receiver_is_connected(vtp_receiver_t* receiver) {
    if (!receiver) return false;
    return reinterpret_cast<VTPReceiver*>(receiver)->IsConnected();
}

// ==========================================
// Discovery Listener APIs
// ==========================================

vtp_listener_t* vtp_create_listener() {
    return reinterpret_cast<vtp_listener_t*>(new VTPDiscoveryListener());
}

void vtp_destroy_listener(vtp_listener_t* listener) {
    if (listener) {
        delete reinterpret_cast<VTPDiscoveryListener*>(listener);
    }
}

void vtp_listener_start(vtp_listener_t* listener) {
    if (listener) {
        reinterpret_cast<VTPDiscoveryListener*>(listener)->Start();
    }
}

void vtp_listener_stop(vtp_listener_t* listener) {
    if (listener) {
        reinterpret_cast<VTPDiscoveryListener*>(listener)->Stop();
    }
}

int vtp_listener_get_sources(vtp_listener_t* listener, vtp_source_t* out_sources, int max_sources) {
    if (!listener) return 0;
    return reinterpret_cast<VTPDiscoveryListener*>(listener)->GetSources(
        reinterpret_cast<VTPSource*>(out_sources), max_sources);
}

} // extern "C"
