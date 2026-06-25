#pragma once

#include <vtp.h>
constexpr uint32_t VTP_MAGIC = 0x21505456;
constexpr uint16_t VTP_DEFAULT_PORT = 40425;

#pragma pack(push, 1)

enum VTPPacketType : uint8_t { VTP_PACKET_VIDEO = 0, VTP_PACKET_AUDIO = 1 };

struct VTPPacketHeader {
  uint32_t magic;
  uint8_t type;
  uint8_t reserved[3];
  uint32_t payload_size;
  uint64_t timestamp_ns;
};

struct VTPAudioMetadata {
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t sample_format;
  uint16_t sample_count;
  uint32_t data_size;
};

#pragma pack(pop)

using VTPPixelFormat = vtp_pixel_format_t;
using VTPFrameHeader = vtp_frame_header_t;
using VTPSource = vtp_source_t;
using VTPFrame = vtp_frame_t;
using VTPAudioFrame = vtp_audio_frame_t;
