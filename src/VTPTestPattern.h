#pragma once

#include <vtp.h>
#include <cstdint>

namespace vtp {

bool GenerateTestPattern(unsigned char *buffer, int width, int height,
                         int stride, vtp_pixel_format_t format,
                         uint32_t frame_index);

bool GenerateTestTone(int16_t *pcm_buffer, int sample_count,
                      uint32_t sample_rate, uint8_t channels,
                      double frequency_hz, uint64_t *sample_phase_inout);

} // namespace vtp
