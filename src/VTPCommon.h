#pragma once

#include <vtp.h>
#include <cstdint>

constexpr uint32_t VTP_MAGIC = 0x21505456;
constexpr uint32_t VTP_DISCOVERY_MAGIC = 0x44505456;
constexpr uint32_t VTP_UDP_MAGIC = 0x55505456;

constexpr uint16_t VTP_DEFAULT_PORT = 40425;
constexpr uint8_t VTP_PROTOCOL_VERSION = 2;

constexpr uint32_t VTP_MAX_FRAME_DIM = 16384;
constexpr uint32_t VTP_MAX_VIDEO_PAYLOAD = 64u * 1024u * 1024u;
constexpr uint32_t VTP_MAX_AUDIO_PAYLOAD = 4u * 1024u * 1024u;
constexpr uint32_t VTP_MAX_META_PAYLOAD = 1024u * 1024u;
constexpr uint8_t VTP_MAX_CHANNELS = 16;
constexpr uint16_t VTP_UDP_MTU_PAYLOAD = 1360;
constexpr uint8_t VTP_UDP_FEC_GROUP_SIZE = 8;

constexpr uint8_t VTP_PKT_FLAG_SLICED = 0x01;
constexpr uint8_t VTP_PKT_FLAG_EXT_META = 0x02;
constexpr uint8_t VTP_PKT_FLAG_PROXY = 0x04;

constexpr uint8_t VTP_DISC_FLAG_ALPHA = 0x01;
constexpr uint8_t VTP_DISC_FLAG_AUDIO = 0x02;
constexpr uint8_t VTP_DISC_FLAG_UDP = 0x04;
constexpr uint8_t VTP_DISC_FLAG_PROXY = 0x08;

constexpr uint16_t VTP_UDP_FLAG_PARITY = 0x01;

#pragma pack(push, 1)

enum VTPPacketType : uint8_t {
  VTP_PACKET_VIDEO = 0,
  VTP_PACKET_AUDIO = 1,
  VTP_PACKET_METADATA = 2,
  VTP_PACKET_TALLY = 3,
  VTP_PACKET_PTZ = 4,
  VTP_PACKET_CONTROL = 5,
  VTP_PACKET_PING = 6,
  VTP_PACKET_PONG = 7,
  VTP_PACKET_UDP_HELLO = 8
};

struct VTPPacketHeader {
  uint32_t magic;
  uint8_t type;
  uint8_t reserved[3];
  uint32_t payload_size;
  uint64_t timestamp_ns;
};

struct VTPFrameExtMeta {
  uint64_t timecode_ns;
  float aspect_ratio;
  uint8_t color_space;
  uint8_t scan_mode;
  uint8_t full_range;
  uint8_t slice_count;
  uint32_t reserved;
};

struct VTPSliceHeader {
  uint16_t y_start;
  uint16_t slice_height;
  uint32_t jpeg_size;
};

struct VTPAudioMetadata {
  uint32_t sample_rate;
  uint8_t channels;
  uint8_t sample_format;
  uint16_t sample_count;
  uint32_t data_size;
};

struct VTPTallyPacket {
  uint8_t on_program;
  uint8_t on_preview;
};

struct VTPPTZPacket {
  float pan;
  float tilt;
  float zoom;
  float focus;
  uint32_t preset_id;
  uint8_t action;
};

struct VTPPingPacket {
  uint64_t send_time_ns;
};

struct VTPDiscoveryPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t flags;
  uint16_t port;
  uint32_t width;
  uint32_t height;
  uint32_t fps;
  char name[128];
};

struct VTPUDPChunkHeader {
  uint32_t magic;
  uint32_t message_seq;
  uint8_t pkt_type;
  uint8_t fec_group_size;
  uint16_t chunk_idx;
  uint16_t data_chunk_count;
  uint16_t chunk_size;
  uint16_t flags;
  uint16_t reserved;
};

#pragma pack(pop)

static_assert(sizeof(VTPPacketHeader) == 20);
static_assert(sizeof(VTPFrameExtMeta) == 20);
static_assert(sizeof(VTPSliceHeader) == 8);
static_assert(sizeof(VTPAudioMetadata) == 12);
static_assert(sizeof(VTPDiscoveryPacket) == 148);
static_assert(sizeof(VTPUDPChunkHeader) == 20);

inline uint8_t vtp_encode_audio_format(vtp_audio_codec_t codec,
                                       vtp_audio_format_t format) {
  return static_cast<uint8_t>(((static_cast<uint8_t>(codec) & 0x0F) << 4) |
                              (static_cast<uint8_t>(format) & 0x0F));
}

inline vtp_audio_codec_t vtp_decode_audio_codec(uint8_t packed) {
  return static_cast<vtp_audio_codec_t>((packed >> 4) & 0x0F);
}

inline vtp_audio_format_t vtp_decode_audio_format(uint8_t packed) {
  return static_cast<vtp_audio_format_t>(packed & 0x0F);
}

using VTPPixelFormat = vtp_pixel_format_t;
using VTPFrameHeader = vtp_frame_header_t;
using VTPSource = vtp_source_t;
using VTPFrame = vtp_frame_t;
using VTPAudioFrame = vtp_audio_frame_t;
