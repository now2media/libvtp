#include "VTPSimd.h"
#include <algorithm>
#include <cstring>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <emmintrin.h>
#include <immintrin.h>
#define VTP_X86_SIMD 1
#endif

namespace vtp {
namespace simd {

#ifdef VTP_X86_SIMD
static bool HasAVX2() {
  static const bool supported = __builtin_cpu_supports("avx2");
  return supported;
}

__attribute__((target("avx2"))) static void
ExtractAlphaRowAVX2(const uint8_t *src, uint8_t *dst, int width) {
  const __m256i mask = _mm256_setr_epi8(
      3, 7, 11, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, 7, 11,
      15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i perm_idx = _mm256_setr_epi32(0, 4, 1, 1, 1, 1, 1, 1);

  int x = 0;
  for (; x + 8 <= width; x += 8) {
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x * 4));
    __m256i shuf = _mm256_shuffle_epi8(px, mask);
    __m256i packed = _mm256_permutevar8x32_epi32(shuf, perm_idx);
    _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + x),
                     _mm256_castsi256_si128(packed));
  }
  for (; x < width; ++x) {
    dst[x] = src[x * 4 + 3];
  }
}

__attribute__((target("avx2"))) static void
InjectAlphaRowAVX2(uint8_t *dst, const uint8_t *alpha, int width) {
  const __m256i shuf_mask = _mm256_setr_epi8(
      -1, -1, -1, 0, -1, -1, -1, 1, -1, -1, -1, 2, -1, -1, -1, 3, -1, -1, -1,
      0, -1, -1, -1, 1, -1, -1, -1, 2, -1, -1, -1, 3);
  const __m256i rgb_mask = _mm256_set1_epi32(0x00FFFFFF);

  int x = 0;
  for (; x + 8 <= width; x += 8) {
    __m128i a_lo = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(alpha + x));
    __m128i a_hi = _mm_srli_si128(a_lo, 4);
    __m256i a_lanes =
        _mm256_inserti128_si256(_mm256_castsi128_si256(a_lo), a_hi, 1);
    __m256i a_placed = _mm256_shuffle_epi8(a_lanes, shuf_mask);
    __m256i px = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(dst + x * 4));
    __m256i out = _mm256_or_si256(_mm256_and_si256(px, rgb_mask), a_placed);
    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x * 4), out);
  }
  for (; x < width; ++x) {
    dst[x * 4 + 3] = alpha[x];
  }
}

__attribute__((target("avx2"))) static void
UnpackUYVYRowAVX2(const uint8_t *src, uint8_t *y_dst, uint8_t *u_dst,
                  uint8_t *v_dst, int width) {
  const __m256i y_shuf = _mm256_setr_epi8(
      1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1, 1, 3, 5, 7, 9,
      11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i u_shuf = _mm256_setr_epi8(
      0, 4, 8, 12, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 0, 4, 8, 12,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i v_shuf = _mm256_setr_epi8(
      2, 6, 10, 14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 2, 6, 10,
      14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i perm_uv = _mm256_setr_epi32(0, 4, 1, 1, 1, 1, 1, 1);

  int x = 0;
  for (; x + 16 <= width; x += 16) {
    __m256i in = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x * 2));
    __m256i y_lane = _mm256_shuffle_epi8(in, y_shuf);
    __m256i y_pack = _mm256_permute4x64_epi64(y_lane, 0xD8);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(y_dst + x),
                     _mm256_castsi256_si128(y_pack));

    __m256i u_lane = _mm256_shuffle_epi8(in, u_shuf);
    __m256i u_pack = _mm256_permutevar8x32_epi32(u_lane, perm_uv);
    _mm_storel_epi64(reinterpret_cast<__m128i *>(u_dst + x / 2),
                     _mm256_castsi256_si128(u_pack));

    __m256i v_lane = _mm256_shuffle_epi8(in, v_shuf);
    __m256i v_pack = _mm256_permutevar8x32_epi32(v_lane, perm_uv);
    _mm_storel_epi64(reinterpret_cast<__m128i *>(v_dst + x / 2),
                     _mm256_castsi256_si128(v_pack));
  }
  for (; x + 1 < width; x += 2) {
    u_dst[x / 2] = src[x * 2 + 0];
    y_dst[x + 0] = src[x * 2 + 1];
    v_dst[x / 2] = src[x * 2 + 2];
    y_dst[x + 1] = src[x * 2 + 3];
  }
}

__attribute__((target("avx2"))) static void
UnpackYUY2RowAVX2(const uint8_t *src, uint8_t *y_dst, uint8_t *u_dst,
                  uint8_t *v_dst, int width) {
  const __m256i y_shuf = _mm256_setr_epi8(
      0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1, 0, 2, 4, 6, 8,
      10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i u_shuf = _mm256_setr_epi8(
      1, 5, 9, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 1, 5, 9, 13,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i v_shuf = _mm256_setr_epi8(
      3, 7, 11, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3, 7, 11,
      15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i perm_uv = _mm256_setr_epi32(0, 4, 1, 1, 1, 1, 1, 1);

  int x = 0;
  for (; x + 16 <= width; x += 16) {
    __m256i in = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x * 2));
    __m256i y_lane = _mm256_shuffle_epi8(in, y_shuf);
    __m256i y_pack = _mm256_permute4x64_epi64(y_lane, 0xD8);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(y_dst + x),
                     _mm256_castsi256_si128(y_pack));

    __m256i u_lane = _mm256_shuffle_epi8(in, u_shuf);
    __m256i u_pack = _mm256_permutevar8x32_epi32(u_lane, perm_uv);
    _mm_storel_epi64(reinterpret_cast<__m128i *>(u_dst + x / 2),
                     _mm256_castsi256_si128(u_pack));

    __m256i v_lane = _mm256_shuffle_epi8(in, v_shuf);
    __m256i v_pack = _mm256_permutevar8x32_epi32(v_lane, perm_uv);
    _mm_storel_epi64(reinterpret_cast<__m128i *>(v_dst + x / 2),
                     _mm256_castsi256_si128(v_pack));
  }
  for (; x + 1 < width; x += 2) {
    y_dst[x + 0] = src[x * 2 + 0];
    u_dst[x / 2] = src[x * 2 + 1];
    y_dst[x + 1] = src[x * 2 + 2];
    v_dst[x / 2] = src[x * 2 + 3];
  }
}

__attribute__((target("avx2"))) static void
SplitNV12UVRowAVX2(const uint8_t *uv, uint8_t *u_dst, uint8_t *v_dst,
                   int chroma_width) {
  const __m256i u_shuf = _mm256_setr_epi8(
      0, 2, 4, 6, 8, 10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1, 0, 2, 4, 6, 8,
      10, 12, 14, -1, -1, -1, -1, -1, -1, -1, -1);
  const __m256i v_shuf = _mm256_setr_epi8(
      1, 3, 5, 7, 9, 11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1, 1, 3, 5, 7, 9,
      11, 13, 15, -1, -1, -1, -1, -1, -1, -1, -1);

  int x = 0;
  for (; x + 16 <= chroma_width; x += 16) {
    __m256i in = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(uv + x * 2));
    __m256i u_pack = _mm256_permute4x64_epi64(_mm256_shuffle_epi8(in, u_shuf), 0xD8);
    __m256i v_pack = _mm256_permute4x64_epi64(_mm256_shuffle_epi8(in, v_shuf), 0xD8);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(u_dst + x),
                     _mm256_castsi256_si128(u_pack));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(v_dst + x),
                     _mm256_castsi256_si128(v_pack));
  }
  for (; x < chroma_width; ++x) {
    u_dst[x] = uv[x * 2 + 0];
    v_dst[x] = uv[x * 2 + 1];
  }
}

static void PackUYVYRowSSE2(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                             uint8_t *dst, int width) {
  int x = 0;
  for (; x + 16 <= width; x += 16) {
    __m128i u8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(u + x / 2));
    __m128i v8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(v + x / 2));
    __m128i uv = _mm_unpacklo_epi8(u8, v8);
    __m128i y16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(y + x));
    __m128i out_lo = _mm_unpacklo_epi8(uv, y16);
    __m128i out_hi = _mm_unpackhi_epi8(uv, y16);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x * 2), out_lo);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x * 2 + 16), out_hi);
  }
  for (; x + 1 < width; x += 2) {
    dst[x * 2 + 0] = u[x / 2];
    dst[x * 2 + 1] = y[x + 0];
    dst[x * 2 + 2] = v[x / 2];
    dst[x * 2 + 3] = y[x + 1];
  }
}

static void PackYUY2RowSSE2(const uint8_t *y, const uint8_t *u, const uint8_t *v,
                             uint8_t *dst, int width) {
  int x = 0;
  for (; x + 16 <= width; x += 16) {
    __m128i u8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(u + x / 2));
    __m128i v8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(v + x / 2));
    __m128i uv = _mm_unpacklo_epi8(u8, v8);
    __m128i y16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(y + x));
    __m128i out_lo = _mm_unpacklo_epi8(y16, uv);
    __m128i out_hi = _mm_unpackhi_epi8(y16, uv);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x * 2), out_lo);
    _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x * 2 + 16), out_hi);
  }
  for (; x + 1 < width; x += 2) {
    dst[x * 2 + 0] = y[x + 0];
    dst[x * 2 + 1] = u[x / 2];
    dst[x * 2 + 2] = y[x + 1];
    dst[x * 2 + 3] = v[x / 2];
  }
}

static void InterleaveUVRowSSE2(const uint8_t *u, const uint8_t *v,
                                 uint8_t *uv_dst, int chroma_width) {
  int x = 0;
  for (; x + 16 <= chroma_width; x += 16) {
    __m128i u16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(u + x));
    __m128i v16 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(v + x));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(uv_dst + x * 2),
                     _mm_unpacklo_epi8(u16, v16));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(uv_dst + x * 2 + 16),
                     _mm_unpackhi_epi8(u16, v16));
  }
  for (; x < chroma_width; ++x) {
    uv_dst[x * 2 + 0] = u[x];
    uv_dst[x * 2 + 1] = v[x];
  }
}
#endif

void ExtractAlphaStrided(const uint8_t *src, int src_stride, uint8_t *dst_alpha,
                         int width, int height) {
  if (!src || !dst_alpha || width <= 0 || height <= 0)
    return;
  if (src_stride <= 0)
    src_stride = width * 4;

#ifdef VTP_X86_SIMD
  if (HasAVX2()) {
    for (int y = 0; y < height; ++y) {
      ExtractAlphaRowAVX2(src + static_cast<size_t>(y) * src_stride,
                          dst_alpha + static_cast<size_t>(y) * width, width);
    }
    return;
  }
#endif

  for (int y = 0; y < height; ++y) {
    const uint8_t *row = src + static_cast<size_t>(y) * src_stride;
    uint8_t *dst_row = dst_alpha + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      dst_row[x] = row[x * 4 + 3];
    }
  }
}

void InjectAlphaStrided(uint8_t *dst, int dst_stride, const uint8_t *src_alpha,
                        int width, int height) {
  if (!dst || !src_alpha || width <= 0 || height <= 0)
    return;
  if (dst_stride <= 0)
    dst_stride = width * 4;

#ifdef VTP_X86_SIMD
  if (HasAVX2()) {
    for (int y = 0; y < height; ++y) {
      InjectAlphaRowAVX2(dst + static_cast<size_t>(y) * dst_stride,
                         src_alpha + static_cast<size_t>(y) * width, width);
    }
    return;
  }
#endif

  for (int y = 0; y < height; ++y) {
    uint8_t *row = dst + static_cast<size_t>(y) * dst_stride;
    const uint8_t *a_row = src_alpha + static_cast<size_t>(y) * width;
    for (int x = 0; x < width; ++x) {
      row[x * 4 + 3] = a_row[x];
    }
  }
}

void UnpackUYVYToYUV422Planes(const uint8_t *src, int src_stride, uint8_t *y_dst,
                               int y_stride, uint8_t *u_dst, int u_stride,
                               uint8_t *v_dst, int v_stride, int width,
                               int height) {
  if (!src || !y_dst || !u_dst || !v_dst || width <= 0 || height <= 0)
    return;
  if (src_stride <= 0)
    src_stride = width * 2;

#ifdef VTP_X86_SIMD
  if (HasAVX2()) {
    for (int y = 0; y < height; ++y) {
      UnpackUYVYRowAVX2(src + static_cast<size_t>(y) * src_stride,
                        y_dst + static_cast<size_t>(y) * y_stride,
                        u_dst + static_cast<size_t>(y) * u_stride,
                        v_dst + static_cast<size_t>(y) * v_stride, width);
    }
    return;
  }
#endif

  for (int y = 0; y < height; ++y) {
    const uint8_t *row = src + static_cast<size_t>(y) * src_stride;
    uint8_t *yr = y_dst + static_cast<size_t>(y) * y_stride;
    uint8_t *ur = u_dst + static_cast<size_t>(y) * u_stride;
    uint8_t *vr = v_dst + static_cast<size_t>(y) * v_stride;
    for (int x = 0; x + 1 < width; x += 2) {
      ur[x / 2] = row[x * 2 + 0];
      yr[x + 0] = row[x * 2 + 1];
      vr[x / 2] = row[x * 2 + 2];
      yr[x + 1] = row[x * 2 + 3];
    }
  }
}

void UnpackYUY2ToYUV422Planes(const uint8_t *src, int src_stride, uint8_t *y_dst,
                               int y_stride, uint8_t *u_dst, int u_stride,
                               uint8_t *v_dst, int v_stride, int width,
                               int height) {
  if (!src || !y_dst || !u_dst || !v_dst || width <= 0 || height <= 0)
    return;
  if (src_stride <= 0)
    src_stride = width * 2;

#ifdef VTP_X86_SIMD
  if (HasAVX2()) {
    for (int y = 0; y < height; ++y) {
      UnpackYUY2RowAVX2(src + static_cast<size_t>(y) * src_stride,
                        y_dst + static_cast<size_t>(y) * y_stride,
                        u_dst + static_cast<size_t>(y) * u_stride,
                        v_dst + static_cast<size_t>(y) * v_stride, width);
    }
    return;
  }
#endif

  for (int y = 0; y < height; ++y) {
    const uint8_t *row = src + static_cast<size_t>(y) * src_stride;
    uint8_t *yr = y_dst + static_cast<size_t>(y) * y_stride;
    uint8_t *ur = u_dst + static_cast<size_t>(y) * u_stride;
    uint8_t *vr = v_dst + static_cast<size_t>(y) * v_stride;
    for (int x = 0; x + 1 < width; x += 2) {
      yr[x + 0] = row[x * 2 + 0];
      ur[x / 2] = row[x * 2 + 1];
      yr[x + 1] = row[x * 2 + 2];
      vr[x / 2] = row[x * 2 + 3];
    }
  }
}

void PackYUV422PlanesToUYVY(const uint8_t *y_src, int y_stride,
                             const uint8_t *u_src, int u_stride,
                             const uint8_t *v_src, int v_stride, uint8_t *dst,
                             int dst_stride, int width, int height) {
  if (!y_src || !u_src || !v_src || !dst || width <= 0 || height <= 0)
    return;
  if (dst_stride <= 0)
    dst_stride = width * 2;

  for (int y = 0; y < height; ++y) {
    const uint8_t *yr = y_src + static_cast<size_t>(y) * y_stride;
    const uint8_t *ur = u_src + static_cast<size_t>(y) * u_stride;
    const uint8_t *vr = v_src + static_cast<size_t>(y) * v_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;
#ifdef VTP_X86_SIMD
    PackUYVYRowSSE2(yr, ur, vr, dr, width);
#else
    for (int x = 0; x + 1 < width; x += 2) {
      dr[x * 2 + 0] = ur[x / 2];
      dr[x * 2 + 1] = yr[x + 0];
      dr[x * 2 + 2] = vr[x / 2];
      dr[x * 2 + 3] = yr[x + 1];
    }
#endif
  }
}

void PackYUV422PlanesToYUY2(const uint8_t *y_src, int y_stride,
                             const uint8_t *u_src, int u_stride,
                             const uint8_t *v_src, int v_stride, uint8_t *dst,
                             int dst_stride, int width, int height) {
  if (!y_src || !u_src || !v_src || !dst || width <= 0 || height <= 0)
    return;
  if (dst_stride <= 0)
    dst_stride = width * 2;

  for (int y = 0; y < height; ++y) {
    const uint8_t *yr = y_src + static_cast<size_t>(y) * y_stride;
    const uint8_t *ur = u_src + static_cast<size_t>(y) * u_stride;
    const uint8_t *vr = v_src + static_cast<size_t>(y) * v_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;
#ifdef VTP_X86_SIMD
    PackYUY2RowSSE2(yr, ur, vr, dr, width);
#else
    for (int x = 0; x + 1 < width; x += 2) {
      dr[x * 2 + 0] = yr[x + 0];
      dr[x * 2 + 1] = ur[x / 2];
      dr[x * 2 + 2] = yr[x + 1];
      dr[x * 2 + 3] = vr[x / 2];
    }
#endif
  }
}

void SplitNV12UVToPlanes(const uint8_t *uv_src, int uv_stride, uint8_t *u_dst,
                         int u_stride, uint8_t *v_dst, int v_stride,
                         int chroma_width, int chroma_height) {
  if (!uv_src || !u_dst || !v_dst || chroma_width <= 0 || chroma_height <= 0)
    return;
  if (uv_stride <= 0)
    uv_stride = chroma_width * 2;

#ifdef VTP_X86_SIMD
  if (HasAVX2()) {
    for (int y = 0; y < chroma_height; ++y) {
      SplitNV12UVRowAVX2(uv_src + static_cast<size_t>(y) * uv_stride,
                         u_dst + static_cast<size_t>(y) * u_stride,
                         v_dst + static_cast<size_t>(y) * v_stride,
                         chroma_width);
    }
    return;
  }
#endif

  for (int y = 0; y < chroma_height; ++y) {
    const uint8_t *row = uv_src + static_cast<size_t>(y) * uv_stride;
    uint8_t *ur = u_dst + static_cast<size_t>(y) * u_stride;
    uint8_t *vr = v_dst + static_cast<size_t>(y) * v_stride;
    for (int x = 0; x < chroma_width; ++x) {
      ur[x] = row[x * 2 + 0];
      vr[x] = row[x * 2 + 1];
    }
  }
}

void InterleavePlanesToNV12UV(const uint8_t *u_src, int u_stride,
                               const uint8_t *v_src, int v_stride,
                               uint8_t *uv_dst, int uv_stride, int chroma_width,
                               int chroma_height) {
  if (!u_src || !v_src || !uv_dst || chroma_width <= 0 || chroma_height <= 0)
    return;
  if (uv_stride <= 0)
    uv_stride = chroma_width * 2;

  for (int y = 0; y < chroma_height; ++y) {
    const uint8_t *ur = u_src + static_cast<size_t>(y) * u_stride;
    const uint8_t *vr = v_src + static_cast<size_t>(y) * v_stride;
    uint8_t *uv = uv_dst + static_cast<size_t>(y) * uv_stride;
#ifdef VTP_X86_SIMD
    InterleaveUVRowSSE2(ur, vr, uv, chroma_width);
#else
    for (int x = 0; x < chroma_width; ++x) {
      uv[x * 2 + 0] = ur[x];
      uv[x * 2 + 1] = vr[x];
    }
#endif
  }
}

void ResampleChroma422To420(const uint8_t *src, int src_stride, uint8_t *dst,
                           int dst_stride, int chroma_width, int src_height) {
  if (!src || !dst || chroma_width <= 0 || src_height <= 0)
    return;
  int dst_height = (src_height + 1) / 2;
  for (int y = 0; y < dst_height; ++y) {
    int y0 = y * 2;
    int y1 = std::min(y0 + 1, src_height - 1);
    const uint8_t *r0 = src + static_cast<size_t>(y0) * src_stride;
    const uint8_t *r1 = src + static_cast<size_t>(y1) * src_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;

    int x = 0;
#ifdef VTP_X86_SIMD
    for (; x + 16 <= chroma_width; x += 16) {
      __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r0 + x));
      __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r1 + x));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(dr + x), _mm_avg_epu8(a, b));
    }
#endif
    for (; x < chroma_width; ++x) {
      dr[x] = static_cast<uint8_t>((static_cast<int>(r0[x]) + r1[x] + 1) >> 1);
    }
  }
}

void ResampleChroma420To422(const uint8_t *src, int src_stride, uint8_t *dst,
                           int dst_stride, int chroma_width, int dst_height) {
  if (!src || !dst || chroma_width <= 0 || dst_height <= 0)
    return;
  int src_height = (dst_height + 1) / 2;
  for (int y = 0; y < dst_height; ++y) {
    int sy = std::min(y / 2, src_height - 1);
    std::memcpy(dst + static_cast<size_t>(y) * dst_stride,
                src + static_cast<size_t>(sy) * src_stride,
                static_cast<size_t>(chroma_width));
  }
}

void DownscalePlaneHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                        int dst_stride, int dst_width, int dst_height) {
  if (!src || !dst || dst_width <= 0 || dst_height <= 0)
    return;
  for (int y = 0; y < dst_height; ++y) {
    const uint8_t *r0 = src + static_cast<size_t>(y * 2) * src_stride;
    const uint8_t *r1 = src + static_cast<size_t>(y * 2 + 1) * src_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;
    for (int x = 0; x < dst_width; ++x) {
      int sum = r0[x * 2] + r0[x * 2 + 1] + r1[x * 2] + r1[x * 2 + 1];
      dr[x] = static_cast<uint8_t>((sum + 2) >> 2);
    }
  }
}

void DownscaleRGBAHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                       int dst_stride, int dst_width, int dst_height) {
  if (!src || !dst || dst_width <= 0 || dst_height <= 0)
    return;
  for (int y = 0; y < dst_height; ++y) {
    const uint8_t *r0 = src + static_cast<size_t>(y * 2) * src_stride;
    const uint8_t *r1 = src + static_cast<size_t>(y * 2 + 1) * src_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;
    int x = 0;
#ifdef VTP_X86_SIMD
    for (; x + 4 <= dst_width; x += 4) {
      __m128i r0a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r0 + x * 8));
      __m128i r0b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r0 + x * 8 + 16));
      __m128i r1a = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r1 + x * 8));
      __m128i r1b = _mm_loadu_si128(reinterpret_cast<const __m128i *>(r1 + x * 8 + 16));

      __m128i v0 = _mm_avg_epu8(r0a, r1a);
      __m128i v1 = _mm_avg_epu8(r0b, r1b);

      __m128i even = _mm_castps_si128(
          _mm_shuffle_ps(_mm_castsi128_ps(v0), _mm_castsi128_ps(v1),
                         _MM_SHUFFLE(2, 0, 2, 0)));
      __m128i odd = _mm_castps_si128(
          _mm_shuffle_ps(_mm_castsi128_ps(v0), _mm_castsi128_ps(v1),
                         _MM_SHUFFLE(3, 1, 3, 1)));
      _mm_storeu_si128(reinterpret_cast<__m128i *>(dr + x * 4),
                       _mm_avg_epu8(even, odd));
    }
#endif
    for (; x < dst_width; ++x) {
      const uint8_t *p00 = r0 + x * 8;
      const uint8_t *p01 = r0 + x * 8 + 4;
      const uint8_t *p10 = r1 + x * 8;
      const uint8_t *p11 = r1 + x * 8 + 4;
      for (int c = 0; c < 4; ++c) {
        dr[x * 4 + c] = static_cast<uint8_t>(
            (p00[c] + p01[c] + p10[c] + p11[c] + 2) >> 2);
      }
    }
  }
}

void DownscaleRGBHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                      int dst_stride, int dst_width, int dst_height) {
  if (!src || !dst || dst_width <= 0 || dst_height <= 0)
    return;
  for (int y = 0; y < dst_height; ++y) {
    const uint8_t *r0 = src + static_cast<size_t>(y * 2) * src_stride;
    const uint8_t *r1 = src + static_cast<size_t>(y * 2 + 1) * src_stride;
    uint8_t *dr = dst + static_cast<size_t>(y) * dst_stride;
    for (int x = 0; x < dst_width; ++x) {
      const uint8_t *p00 = r0 + x * 6;
      const uint8_t *p01 = r0 + x * 6 + 3;
      const uint8_t *p10 = r1 + x * 6;
      const uint8_t *p11 = r1 + x * 6 + 3;
      for (int c = 0; c < 3; ++c) {
        dr[x * 3 + c] = static_cast<uint8_t>(
            (p00[c] + p01[c] + p10[c] + p11[c] + 2) >> 2);
      }
    }
  }
}

void DownscaleGrayHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                       int dst_stride, int dst_width, int dst_height) {
  DownscalePlaneHalf(src, src_stride, dst, dst_stride, dst_width, dst_height);
}

}
} // namespace vtp
