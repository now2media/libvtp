#include "VTPAudioCodec.h"
#include "VTPLog.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef VTP_HAS_OPUS
#include <opus/opus.h>
#include <opus/opus_multistream.h>
#endif

namespace vtp {
namespace audio {

size_t BytesPerSample(vtp_audio_format_t fmt) {
  switch (fmt) {
  case VTP_AUDIO_PCM_S16LE:
    return 2;
  case VTP_AUDIO_PCM_S24LE:
    return 3;
  case VTP_AUDIO_PCM_S32LE:
  case VTP_AUDIO_PCM_F32LE:
  case VTP_AUDIO_PCM_F32LE_PLANAR:
    return 4;
  default:
    return 2;
  }
}

size_t AudioPayloadSize(vtp_audio_format_t fmt, int sample_count,
                        int channels) {
  if (sample_count <= 0 || channels <= 0)
    return 0;
  return static_cast<size_t>(sample_count) * static_cast<size_t>(channels) *
         BytesPerSample(fmt);
}

void ConvertToS16LE(const void *src, vtp_audio_format_t src_fmt,
                    int sample_count, int channels, std::vector<int16_t> &dst) {
  if (!src || sample_count <= 0 || channels <= 0) {
    dst.clear();
    return;
  }
  const size_t total =
      static_cast<size_t>(sample_count) * static_cast<size_t>(channels);
  dst.resize(total);

  switch (src_fmt) {
  case VTP_AUDIO_PCM_S16LE:
    std::memcpy(dst.data(), src, total * sizeof(int16_t));
    break;

  case VTP_AUDIO_PCM_S24LE: {
    const uint8_t *p = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < total; ++i) {
      uint32_t hi = (static_cast<uint32_t>(p[i * 3 + 2]) << 24) |
                    (static_cast<uint32_t>(p[i * 3 + 1]) << 16);
      dst[i] = static_cast<int16_t>(static_cast<int32_t>(hi) >> 16);
    }
    break;
  }

  case VTP_AUDIO_PCM_S32LE: {
    const int32_t *p = static_cast<const int32_t *>(src);
    for (size_t i = 0; i < total; ++i) {
      dst[i] = static_cast<int16_t>(p[i] >> 16);
    }
    break;
  }

  case VTP_AUDIO_PCM_F32LE: {
    const float *p = static_cast<const float *>(src);
    for (size_t i = 0; i < total; ++i) {
      float v = std::max(-1.0f, std::min(1.0f, p[i]));
      dst[i] = static_cast<int16_t>(std::lrintf(v * 32767.0f));
    }
    break;
  }

  case VTP_AUDIO_PCM_F32LE_PLANAR: {
    const float *p = static_cast<const float *>(src);
    for (int i = 0; i < sample_count; ++i) {
      for (int c = 0; c < channels; ++c) {
        float v = p[static_cast<size_t>(c) * sample_count + i];
        v = std::max(-1.0f, std::min(1.0f, v));
        dst[static_cast<size_t>(i) * channels + c] =
            static_cast<int16_t>(std::lrintf(v * 32767.0f));
      }
    }
    break;
  }

  default:
    std::memset(dst.data(), 0, total * sizeof(int16_t));
    break;
  }
}

void ConvertToFloatInterleaved(const void *src, vtp_audio_format_t src_fmt,
                               int sample_count, int channels,
                               std::vector<float> &dst) {
  if (!src || sample_count <= 0 || channels <= 0) {
    dst.clear();
    return;
  }
  const size_t total =
      static_cast<size_t>(sample_count) * static_cast<size_t>(channels);
  dst.resize(total);

  switch (src_fmt) {
  case VTP_AUDIO_PCM_S16LE: {
    const int16_t *p = static_cast<const int16_t *>(src);
    for (size_t i = 0; i < total; ++i) {
      dst[i] = static_cast<float>(p[i]) / 32768.0f;
    }
    break;
  }

  case VTP_AUDIO_PCM_S24LE: {
    const uint8_t *p = static_cast<const uint8_t *>(src);
    for (size_t i = 0; i < total; ++i) {
      uint32_t raw = (static_cast<uint32_t>(p[i * 3 + 2]) << 24) |
                     (static_cast<uint32_t>(p[i * 3 + 1]) << 16) |
                     (static_cast<uint32_t>(p[i * 3 + 0]) << 8);
      int32_t signed24 = static_cast<int32_t>(raw) >> 8;
      dst[i] = static_cast<float>(signed24) / 8388608.0f;
    }
    break;
  }

  case VTP_AUDIO_PCM_S32LE: {
    const int32_t *p = static_cast<const int32_t *>(src);
    for (size_t i = 0; i < total; ++i) {
      dst[i] = static_cast<float>(static_cast<double>(p[i]) / 2147483648.0);
    }
    break;
  }

  case VTP_AUDIO_PCM_F32LE:
    std::memcpy(dst.data(), src, total * sizeof(float));
    break;

  case VTP_AUDIO_PCM_F32LE_PLANAR: {
    const float *p = static_cast<const float *>(src);
    for (int i = 0; i < sample_count; ++i) {
      for (int c = 0; c < channels; ++c) {
        dst[static_cast<size_t>(i) * channels + c] =
            p[static_cast<size_t>(c) * sample_count + i];
      }
    }
    break;
  }

  default:
    std::fill(dst.begin(), dst.end(), 0.0f);
    break;
  }
}

static bool IsValidOpusSampleRate(uint32_t sr) {
  return sr == 8000 || sr == 12000 || sr == 16000 || sr == 24000 || sr == 48000;
}

struct VTPOpusEncoder::Impl {
#ifdef VTP_HAS_OPUS
  OpusEncoder *enc = nullptr;
  OpusMSEncoder *ms_enc = nullptr;
#endif
};

VTPOpusEncoder::VTPOpusEncoder() : impl_(new Impl()) {}

VTPOpusEncoder::~VTPOpusEncoder() {
  Reset();
  delete impl_;
  impl_ = nullptr;
}

bool VTPOpusEncoder::IsAvailable() const {
#ifdef VTP_HAS_OPUS
  return true;
#else
  return false;
#endif
}

void VTPOpusEncoder::Reset() {
#ifdef VTP_HAS_OPUS
  if (impl_->enc) {
    opus_encoder_destroy(impl_->enc);
    impl_->enc = nullptr;
  }
  if (impl_->ms_enc) {
    opus_multistream_encoder_destroy(impl_->ms_enc);
    impl_->ms_enc = nullptr;
  }
#endif
  sample_rate_ = 0;
  channels_ = 0;
  frame_size_ = 0;
  fifo_.clear();
}

bool VTPOpusEncoder::Init(uint32_t sample_rate, uint8_t channels, int bitrate) {
#ifdef VTP_HAS_OPUS
  if (!IsValidOpusSampleRate(sample_rate) || channels == 0 ||
      channels > VTP_MAX_CHANNELS) {
    VTP_LOG_WARN("Opus encoder unsupported rate/channels: %u Hz, %u ch",
                 sample_rate, channels);
    return false;
  }

  Reset();
  int err = OPUS_OK;
  if (channels <= 2) {
    impl_->enc = opus_encoder_create(static_cast<opus_int32>(sample_rate),
                                     channels,
                                     OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (!impl_->enc || err != OPUS_OK) {
      VTP_LOG_ERROR("opus_encoder_create failed: %d", err);
      Reset();
      return false;
    }
    if (bitrate > 0) {
      opus_encoder_ctl(impl_->enc, OPUS_SET_BITRATE(bitrate));
    }
  } else {
    unsigned char mapping[VTP_MAX_CHANNELS] = {};
    for (uint8_t i = 0; i < channels; ++i) {
      mapping[i] = i;
    }
    impl_->ms_enc = opus_multistream_encoder_create(
        static_cast<opus_int32>(sample_rate), channels, channels, 0, mapping,
        OPUS_APPLICATION_RESTRICTED_LOWDELAY, &err);
    if (!impl_->ms_enc || err != OPUS_OK) {
      VTP_LOG_ERROR("opus_multistream_encoder_create failed: %d", err);
      Reset();
      return false;
    }
    if (bitrate > 0) {
      opus_multistream_encoder_ctl(impl_->ms_enc, OPUS_SET_BITRATE(bitrate));
    }
  }

  sample_rate_ = sample_rate;
  channels_ = channels;
  bitrate_ = bitrate;
  frame_size_ = static_cast<int>(sample_rate / 200);
  return true;
#else
  (void)sample_rate;
  (void)channels;
  (void)bitrate;
  return false;
#endif
}

bool VTPOpusEncoder::Encode(const void *src, vtp_audio_format_t src_fmt,
                            int sample_count, uint32_t sample_rate,
                            uint8_t channels, uint64_t timestamp_ns,
                            std::vector<EncodedOpusPacket> &out_packets) {
  out_packets.clear();
#ifdef VTP_HAS_OPUS
  if (!src || sample_count <= 0 || channels == 0)
    return false;

  if (sample_rate != sample_rate_ || channels != channels_) {
    if (!Init(sample_rate, channels, bitrate_)) {
      return false;
    }
  }

  size_t prev_samples = fifo_.size() / channels_;
  ConvertToFloatInterleaved(src, src_fmt, sample_count, channels_, scratch_);
  fifo_.insert(fifo_.end(), scratch_.begin(), scratch_.end());

  const uint64_t ns_per_sample = 1000000000ULL / sample_rate_;
  uint64_t base_ts = timestamp_ns;
  if (prev_samples > 0 && base_ts > prev_samples * ns_per_sample) {
    base_ts -= prev_samples * ns_per_sample;
  }

  const size_t chunk_floats = static_cast<size_t>(frame_size_) * channels_;
  size_t offset = 0;
  uint64_t pkt_idx = 0;

  while (fifo_.size() - offset >= chunk_floats) {
    EncodedOpusPacket pkt;
    pkt.data.resize(4096 * channels_);
    int encoded = -1;
    if (impl_->enc) {
      encoded = opus_encode_float(impl_->enc, fifo_.data() + offset,
                                  frame_size_, pkt.data.data(),
                                  static_cast<opus_int32>(pkt.data.size()));
    } else if (impl_->ms_enc) {
      encoded = opus_multistream_encode_float(
          impl_->ms_enc, fifo_.data() + offset, frame_size_, pkt.data.data(),
          static_cast<opus_int32>(pkt.data.size()));
    }

    if (encoded < 0) {
      VTP_LOG_ERROR("Opus encode failed: %d", encoded);
      return false;
    }

    pkt.data.resize(static_cast<size_t>(encoded));
    pkt.sample_count = static_cast<uint16_t>(frame_size_);
    pkt.timestamp_ns = base_ts + pkt_idx * static_cast<uint64_t>(frame_size_) *
                                     1000000000ULL / sample_rate_;
    out_packets.push_back(std::move(pkt));

    offset += chunk_floats;
    ++pkt_idx;
  }

  if (offset > 0) {
    fifo_.erase(fifo_.begin(), fifo_.begin() + static_cast<ptrdiff_t>(offset));
  }
  return true;
#else
  (void)src;
  (void)src_fmt;
  (void)sample_count;
  (void)sample_rate;
  (void)channels;
  (void)timestamp_ns;
  return false;
#endif
}

struct VTPOpusDecoder::Impl {
#ifdef VTP_HAS_OPUS
  OpusDecoder *dec = nullptr;
  OpusMSDecoder *ms_dec = nullptr;
#endif
};

VTPOpusDecoder::VTPOpusDecoder() : impl_(new Impl()) {}

VTPOpusDecoder::~VTPOpusDecoder() {
  Reset();
  delete impl_;
  impl_ = nullptr;
}

bool VTPOpusDecoder::IsAvailable() const {
#ifdef VTP_HAS_OPUS
  return true;
#else
  return false;
#endif
}

void VTPOpusDecoder::Reset() {
#ifdef VTP_HAS_OPUS
  if (impl_->dec) {
    opus_decoder_destroy(impl_->dec);
    impl_->dec = nullptr;
  }
  if (impl_->ms_dec) {
    opus_multistream_decoder_destroy(impl_->ms_dec);
    impl_->ms_dec = nullptr;
  }
#endif
  sample_rate_ = 0;
  channels_ = 0;
}

bool VTPOpusDecoder::Init(uint32_t sample_rate, uint8_t channels) {
#ifdef VTP_HAS_OPUS
  if (!IsValidOpusSampleRate(sample_rate) || channels == 0 ||
      channels > VTP_MAX_CHANNELS) {
    return false;
  }

  Reset();
  int err = OPUS_OK;
  if (channels <= 2) {
    impl_->dec = opus_decoder_create(static_cast<opus_int32>(sample_rate),
                                     channels, &err);
    if (!impl_->dec || err != OPUS_OK) {
      Reset();
      return false;
    }
  } else {
    unsigned char mapping[VTP_MAX_CHANNELS] = {};
    for (uint8_t i = 0; i < channels; ++i) {
      mapping[i] = i;
    }
    impl_->ms_dec = opus_multistream_decoder_create(
        static_cast<opus_int32>(sample_rate), channels, channels, 0, mapping,
        &err);
    if (!impl_->ms_dec || err != OPUS_OK) {
      Reset();
      return false;
    }
  }

  sample_rate_ = sample_rate;
  channels_ = channels;
  return true;
#else
  (void)sample_rate;
  (void)channels;
  return false;
#endif
}

int VTPOpusDecoder::DecodeToFloat(const uint8_t *payload, int payload_size,
                                  uint32_t sample_rate, uint8_t channels,
                                  std::vector<float> &out_pcm) {
#ifdef VTP_HAS_OPUS
  if (!payload || payload_size <= 0 || channels == 0)
    return -1;
  if (sample_rate != sample_rate_ || channels != channels_) {
    if (!Init(sample_rate, channels))
      return -1;
  }

  constexpr int MAX_OPUS_FRAME_SAMPLES = 5760;
  out_pcm.resize(static_cast<size_t>(MAX_OPUS_FRAME_SAMPLES) * channels_);

  int decoded = -1;
  if (impl_->dec) {
    decoded = opus_decode_float(impl_->dec, payload, payload_size,
                                out_pcm.data(), MAX_OPUS_FRAME_SAMPLES, 0);
  } else if (impl_->ms_dec) {
    decoded = opus_multistream_decode_float(impl_->ms_dec, payload,
                                            payload_size, out_pcm.data(),
                                            MAX_OPUS_FRAME_SAMPLES, 0);
  }

  if (decoded < 0) {
    out_pcm.clear();
    return decoded;
  }
  out_pcm.resize(static_cast<size_t>(decoded) * channels_);
  return decoded;
#else
  (void)payload;
  (void)payload_size;
  (void)sample_rate;
  (void)channels;
  out_pcm.clear();
  return -1;
#endif
}

}
} // namespace vtp
