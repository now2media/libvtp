#pragma once

#include <vtp.h>

// Magic bytes: "VTP!" (0x56, 0x54, 0x50, 0x21) in Little Endian is 0x21505456
constexpr uint32_t VTP_MAGIC = 0x21505456; 

// Default port for VTP streams
constexpr uint16_t VTP_DEFAULT_PORT = 40425;

#pragma pack(push, 1)

enum VTPPacketType : uint8_t {
    VTP_PACKET_VIDEO = 0,
    VTP_PACKET_AUDIO = 1
};

struct VTPPacketHeader {
    uint32_t magic;         // Must be VTP_MAGIC
    uint8_t type;           // VTPPacketType
    uint8_t reserved[3];    // Padding
    uint32_t payload_size;  // Size of the payload following this header in bytes
    uint64_t timestamp_ns;  // Timestamp in nanoseconds
};

struct VTPAudioMetadata {
    uint32_t sample_rate;   // e.g. 48000
    uint8_t channels;       // e.g. 2 (stereo)
    uint8_t sample_format;  // e.g. 0 = 16-bit Signed PCM
    uint16_t sample_count;  // Number of samples per channel in this packet
    uint32_t data_size;     // Size of the raw PCM buffer in bytes (sample_count * channels * bytes_per_sample)
};

#pragma pack(pop)

// Type aliases for cleaner internal C++ code
using VTPPixelFormat = vtp_pixel_format_t;
using VTPFrameHeader = vtp_frame_header_t;
using VTPSource = vtp_source_t;
using VTPFrame = vtp_frame_t;
using VTPAudioFrame = vtp_audio_frame_t;
