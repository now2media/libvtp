#pragma once

#include <cstdint>

namespace vtp {
namespace simd {

void ExtractAlphaStrided(const uint8_t *src, int src_stride, uint8_t *dst_alpha,
                         int width, int height);

void InjectAlphaStrided(uint8_t *dst, int dst_stride, const uint8_t *src_alpha,
                        int width, int height);

void UnpackUYVYToYUV422Planes(const uint8_t *src, int src_stride, uint8_t *y_dst,
                               int y_stride, uint8_t *u_dst, int u_stride,
                               uint8_t *v_dst, int v_stride, int width,
                               int height);

void UnpackYUY2ToYUV422Planes(const uint8_t *src, int src_stride, uint8_t *y_dst,
                               int y_stride, uint8_t *u_dst, int u_stride,
                               uint8_t *v_dst, int v_stride, int width,
                               int height);

void PackYUV422PlanesToUYVY(const uint8_t *y_src, int y_stride,
                             const uint8_t *u_src, int u_stride,
                             const uint8_t *v_src, int v_stride, uint8_t *dst,
                             int dst_stride, int width, int height);

void PackYUV422PlanesToYUY2(const uint8_t *y_src, int y_stride,
                             const uint8_t *u_src, int u_stride,
                             const uint8_t *v_src, int v_stride, uint8_t *dst,
                             int dst_stride, int width, int height);

void SplitNV12UVToPlanes(const uint8_t *uv_src, int uv_stride, uint8_t *u_dst,
                         int u_stride, uint8_t *v_dst, int v_stride,
                         int chroma_width, int chroma_height);

void InterleavePlanesToNV12UV(const uint8_t *u_src, int u_stride,
                               const uint8_t *v_src, int v_stride,
                               uint8_t *uv_dst, int uv_stride, int chroma_width,
                               int chroma_height);

void ResampleChroma422To420(const uint8_t *src, int src_stride, uint8_t *dst,
                           int dst_stride, int chroma_width, int src_height);

void ResampleChroma420To422(const uint8_t *src, int src_stride, uint8_t *dst,
                           int dst_stride, int chroma_width, int dst_height);

void DownscalePlaneHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                        int dst_stride, int dst_width, int dst_height);

void DownscaleRGBAHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                       int dst_stride, int dst_width, int dst_height);

void DownscaleRGBHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                      int dst_stride, int dst_width, int dst_height);

void DownscaleGrayHalf(const uint8_t *src, int src_stride, uint8_t *dst,
                       int dst_stride, int dst_width, int dst_height);

}
} // namespace vtp
