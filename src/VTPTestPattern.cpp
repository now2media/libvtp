#include "VTPTestPattern.h"
#include "VTPCommon.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vtp {

struct ColorRGB {
  uint8_t r, g, b;
};

constexpr ColorRGB SMPTE_BARS[7] = {
    {191, 191, 191}, {191, 191, 0}, {0, 191, 191}, {0, 191, 0},
    {191, 0, 191},   {191, 0, 0},   {0, 0, 191},
};

constexpr ColorRGB CAST_BARS[7] = {
    {0, 0, 191},     {16, 16, 16},  {191, 0, 191}, {16, 16, 16},
    {0, 191, 191},   {16, 16, 16},  {191, 191, 191},
};

static void RgbToYuvBT709(uint8_t r, uint8_t g, uint8_t b, uint8_t &y,
                           uint8_t &u, uint8_t &v) {
  int yi = ((47 * r + 157 * g + 16 * b + 128) >> 8) + 16;
  int ui = ((-26 * r - 87 * g + 112 * b + 128) >> 8) + 128;
  int vi = ((112 * r - 102 * g - 10 * b + 128) >> 8) + 128;
  y = static_cast<uint8_t>(std::max(0, std::min(255, yi)));
  u = static_cast<uint8_t>(std::max(0, std::min(255, ui)));
  v = static_cast<uint8_t>(std::max(0, std::min(255, vi)));
}

static void SamplePatternPixel(int x, int y, int width, int height,
                               uint32_t frame_idx, uint8_t &r, uint8_t &g,
                               uint8_t &b, uint8_t &a) {
  a = 255;
  const int bar_h = (height * 67) / 100;
  const int cast_h = (height * 75) / 100;

  if (y < bar_h) {
    int idx = std::min(6, (x * 7) / width);
    r = SMPTE_BARS[idx].r;
    g = SMPTE_BARS[idx].g;
    b = SMPTE_BARS[idx].b;
  } else if (y < cast_h) {
    int idx = std::min(6, (x * 7) / width);
    r = CAST_BARS[idx].r;
    g = CAST_BARS[idx].g;
    b = CAST_BARS[idx].b;
  } else {
    const int box_w = std::max(16, width / 16);
    const int box_x = static_cast<int>((frame_idx * 8u) %
                                       static_cast<uint32_t>(width));
    int dx = x - box_x;
    if (dx < 0)
      dx += width;
    if (dx < box_w) {
      r = g = b = 235;
    } else if (y < (height * 88) / 100) {
      uint8_t luma = static_cast<uint8_t>((x * 235) / std::max(1, width - 1));
      r = g = b = luma;
    } else {
      r = g = b = 24;
      a = static_cast<uint8_t>(64 + ((x * 191) / std::max(1, width - 1)));
    }
  }
}

bool GenerateTestPattern(unsigned char *buffer, int width, int height,
                         int stride, vtp_pixel_format_t format,
                         uint32_t frame_index) {
  if (!buffer || width <= 0 || height <= 0 ||
      width > static_cast<int>(VTP_MAX_FRAME_DIM) ||
      height > static_cast<int>(VTP_MAX_FRAME_DIM)) {
    return false;
  }

  switch (format) {
  case VTP_FORMAT_RGB:
  case VTP_FORMAT_BGR: {
    int eff_stride = stride > 0 ? stride : width * 3;
    bool is_bgr = (format == VTP_FORMAT_BGR);
    for (int y = 0; y < height; ++y) {
      uint8_t *row = buffer + static_cast<size_t>(y) * eff_stride;
      for (int x = 0; x < width; ++x) {
        uint8_t r, g, b, a;
        SamplePatternPixel(x, y, width, height, frame_index, r, g, b, a);
        row[x * 3 + 0] = is_bgr ? b : r;
        row[x * 3 + 1] = g;
        row[x * 3 + 2] = is_bgr ? r : b;
      }
    }
    return true;
  }

  case VTP_FORMAT_RGBA:
  case VTP_FORMAT_BGRA: {
    int eff_stride = stride > 0 ? stride : width * 4;
    bool is_bgra = (format == VTP_FORMAT_BGRA);
    for (int y = 0; y < height; ++y) {
      uint8_t *row = buffer + static_cast<size_t>(y) * eff_stride;
      for (int x = 0; x < width; ++x) {
        uint8_t r, g, b, a;
        SamplePatternPixel(x, y, width, height, frame_index, r, g, b, a);
        row[x * 4 + 0] = is_bgra ? b : r;
        row[x * 4 + 1] = g;
        row[x * 4 + 2] = is_bgra ? r : b;
        row[x * 4 + 3] = a;
      }
    }
    return true;
  }

  case VTP_FORMAT_UYVY:
  case VTP_FORMAT_YUY2: {
    if ((width & 1) != 0)
      return false;
    int eff_stride = stride > 0 ? stride : width * 2;
    bool uyvy = (format == VTP_FORMAT_UYVY);
    for (int y = 0; y < height; ++y) {
      uint8_t *row = buffer + static_cast<size_t>(y) * eff_stride;
      for (int x = 0; x < width; x += 2) {
        uint8_t r0, g0, b0, a0, r1, g1, b1, a1;
        SamplePatternPixel(x, y, width, height, frame_index, r0, g0, b0, a0);
        SamplePatternPixel(x + 1, y, width, height, frame_index, r1, g1, b1,
                           a1);
        uint8_t y0, u0, v0, y1, u1, v1;
        RgbToYuvBT709(r0, g0, b0, y0, u0, v0);
        RgbToYuvBT709(r1, g1, b1, y1, u1, v1);
        uint8_t u = static_cast<uint8_t>((u0 + u1) >> 1);
        uint8_t v = static_cast<uint8_t>((v0 + v1) >> 1);
        if (uyvy) {
          row[x * 2 + 0] = u;
          row[x * 2 + 1] = y0;
          row[x * 2 + 2] = v;
          row[x * 2 + 3] = y1;
        } else {
          row[x * 2 + 0] = y0;
          row[x * 2 + 1] = u;
          row[x * 2 + 2] = y1;
          row[x * 2 + 3] = v;
        }
      }
    }
    return true;
  }

  case VTP_FORMAT_I420:
  case VTP_FORMAT_YV12:
  case VTP_FORMAT_NV12:
  case VTP_FORMAT_I422: {
    if ((width & 1) != 0 || (height & 1) != 0)
      return false;
    int y_stride = stride > 0 ? stride : width;
    int cw = width / 2;
    int ch = (format == VTP_FORMAT_I422) ? height : (height / 2);
    uint8_t *y_plane = buffer;
    uint8_t *c1 = buffer + static_cast<size_t>(y_stride) * height;
    uint8_t *c2 = c1 + static_cast<size_t>(cw) * ch;

    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        uint8_t r, g, b, a;
        SamplePatternPixel(x, y, width, height, frame_index, r, g, b, a);
        uint8_t py, pu, pv;
        RgbToYuvBT709(r, g, b, py, pu, pv);
        y_plane[static_cast<size_t>(y) * y_stride + x] = py;

        bool sample_chroma =
            (format == VTP_FORMAT_I422) ? ((x & 1) == 0)
                                        : ((x & 1) == 0 && (y & 1) == 0);
        if (sample_chroma) {
          int cy = (format == VTP_FORMAT_I422) ? y : (y / 2);
          int cx = x / 2;
          if (format == VTP_FORMAT_NV12) {
            c1[static_cast<size_t>(cy) * y_stride + cx * 2 + 0] = pu;
            c1[static_cast<size_t>(cy) * y_stride + cx * 2 + 1] = pv;
          } else if (format == VTP_FORMAT_YV12) {
            c1[static_cast<size_t>(cy) * cw + cx] = pv;
            c2[static_cast<size_t>(cy) * cw + cx] = pu;
          } else {
            c1[static_cast<size_t>(cy) * cw + cx] = pu;
            c2[static_cast<size_t>(cy) * cw + cx] = pv;
          }
        }
      }
    }
    return true;
  }

  default:
    return false;
  }
}

bool GenerateTestTone(int16_t *pcm_buffer, int sample_count,
                      uint32_t sample_rate, uint8_t channels,
                      double frequency_hz, uint64_t *sample_phase_inout) {
  if (!pcm_buffer || sample_count <= 0 || sample_rate == 0 || channels == 0)
    return false;

  constexpr double TWO_PI = 6.28318530717958647692;
  constexpr double TONE_AMPLITUDE = 4125.0;
  uint64_t phase = sample_phase_inout ? *sample_phase_inout : 0;

  for (int i = 0; i < sample_count; ++i) {
    double t = static_cast<double>(phase + static_cast<uint64_t>(i)) /
               static_cast<double>(sample_rate);
    int16_t val = static_cast<int16_t>(
        std::lrint(TONE_AMPLITUDE * std::sin(TWO_PI * frequency_hz * t)));
    for (uint8_t c = 0; c < channels; ++c) {
      pcm_buffer[static_cast<size_t>(i) * channels + c] = val;
    }
  }

  if (sample_phase_inout) {
    *sample_phase_inout = phase + static_cast<uint64_t>(sample_count);
  }
  return true;
}

} // namespace vtp
