#include "VTPVideoCodec.h"
#include "VTPLog.h"
#include "VTPSimd.h"
#include <algorithm>
#include <cstring>

namespace vtp {

VTPJpegWorkerPool::VTPJpegWorkerPool(int num_threads, bool compress_mode) {
  int count = std::max(1, std::min(num_threads, 16));
  handles_.resize(static_cast<size_t>(count), nullptr);
  for (int i = 0; i < count; ++i) {
    handles_[static_cast<size_t>(i)] =
        compress_mode ? tjInitCompress() : tjInitDecompress();
  }
  for (int i = 1; i < count; ++i) {
    workers_.emplace_back(&VTPJpegWorkerPool::WorkerLoop, this,
                          static_cast<size_t>(i));
  }
}

VTPJpegWorkerPool::~VTPJpegWorkerPool() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
    ++generation_;
  }
  cv_start_.notify_all();
  for (auto &t : workers_) {
    if (t.joinable())
      t.join();
  }
  for (tjhandle h : handles_) {
    if (h)
      tjDestroy(h);
  }
}

void VTPJpegWorkerPool::WorkerLoop(size_t worker_id) {
  uint64_t seen_gen = 0;
  while (true) {
    const std::function<void(size_t, tjhandle)> *fn = nullptr;
    size_t tasks = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_start_.wait(lock,
                     [&]() { return stop_ || generation_ != seen_gen; });
      if (stop_)
        return;
      seen_gen = generation_;
      fn = current_fn_;
      tasks = total_tasks_;
    }

    while (true) {
      size_t idx = next_task_.fetch_add(1, std::memory_order_relaxed);
      if (idx >= tasks)
        break;
      if (fn)
        (*fn)(idx, handles_[worker_id]);
    }

    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (--active_workers_ == 0) {
        cv_done_.notify_one();
      }
    }
  }
}

void VTPJpegWorkerPool::RunParallel(
    size_t task_count,
    const std::function<void(size_t task_idx, tjhandle tj)> &fn) {
  if (task_count == 0)
    return;
  if (task_count == 1 || workers_.empty()) {
    for (size_t i = 0; i < task_count; ++i) {
      fn(i, handles_[0]);
    }
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    current_fn_ = &fn;
    total_tasks_ = task_count;
    next_task_.store(0, std::memory_order_relaxed);
    active_workers_ = workers_.size();
    ++generation_;
  }
  cv_start_.notify_all();

  while (true) {
    size_t idx = next_task_.fetch_add(1, std::memory_order_relaxed);
    if (idx >= task_count)
      break;
    fn(idx, handles_[0]);
  }

  std::unique_lock<std::mutex> lock(mutex_);
  cv_done_.wait(lock, [&]() { return active_workers_ == 0; });
  current_fn_ = nullptr;
}

bool IsYUVPixelFormat(vtp_pixel_format_t fmt) {
  switch (fmt) {
  case VTP_FORMAT_I420:
  case VTP_FORMAT_YV12:
  case VTP_FORMAT_NV12:
  case VTP_FORMAT_UYVY:
  case VTP_FORMAT_YUY2:
  case VTP_FORMAT_I422:
    return true;
  default:
    return false;
  }
}

struct SliceRef {
  uint16_t y_start = 0;
  uint16_t slice_height = 0;
  const uint8_t *jpeg = nullptr;
  uint32_t jpeg_size = 0;
};

static bool ParseSlices(const uint8_t *buf, size_t len, uint32_t frame_height,
                        std::vector<SliceRef> &out_slices) {
  out_slices.clear();
  size_t off = 0;
  while (off < len) {
    if (off + sizeof(VTPSliceHeader) > len)
      return false;
    VTPSliceHeader sh;
    std::memcpy(&sh, buf + off, sizeof(sh));
    off += sizeof(sh);
    if (sh.jpeg_size == 0 || off + sh.jpeg_size > len)
      return false;
    if (static_cast<uint32_t>(sh.y_start) + sh.slice_height > frame_height ||
        sh.slice_height == 0) {
      return false;
    }
    SliceRef ref;
    ref.y_start = sh.y_start;
    ref.slice_height = sh.slice_height;
    ref.jpeg = buf + off;
    ref.jpeg_size = sh.jpeg_size;
    out_slices.push_back(ref);
    off += sh.jpeg_size;
  }
  return !out_slices.empty();
}

static int ToTJPixelFormat(vtp_pixel_format_t fmt, int &out_bpp) {
  switch (fmt) {
  case VTP_FORMAT_RGB:
    out_bpp = 3;
    return TJPF_RGB;
  case VTP_FORMAT_RGBA:
    out_bpp = 4;
    return TJPF_RGBA;
  case VTP_FORMAT_BGR:
    out_bpp = 3;
    return TJPF_BGR;
  case VTP_FORMAT_BGRA:
    out_bpp = 4;
    return TJPF_BGRA;
  default:
    out_bpp = 4;
    return TJPF_RGBA;
  }
}

static bool DecodeToPackedRGB(const std::vector<SliceRef> &slices, uint32_t width,
                              uint32_t height, vtp_pixel_format_t target_fmt,
                              VTPJpegWorkerPool &pool,
                              std::vector<uint8_t> &out_buf,
                              uint32_t &out_stride) {
  int bpp = 4;
  int tj_pf = ToTJPixelFormat(target_fmt, bpp);
  out_stride = width * static_cast<uint32_t>(bpp);
  out_buf.resize(static_cast<size_t>(out_stride) * height);

  std::atomic<bool> ok{true};
  pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
    const SliceRef &s = slices[idx];
    uint8_t *dst =
        out_buf.data() + static_cast<size_t>(s.y_start) * out_stride;
    if (tjDecompress2(tj, s.jpeg, s.jpeg_size, dst, static_cast<int>(width),
                      static_cast<int>(out_stride),
                      static_cast<int>(s.slice_height), tj_pf, 0) != 0) {
      ok.store(false, std::memory_order_relaxed);
    }
  });
  return ok.load(std::memory_order_relaxed);
}

static bool DecodeToGray(const std::vector<SliceRef> &slices, uint32_t width,
                         uint32_t height, VTPJpegWorkerPool &pool,
                         std::vector<uint8_t> &out_buf) {
  out_buf.resize(static_cast<size_t>(width) * height);
  std::atomic<bool> ok{true};
  pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
    const SliceRef &s = slices[idx];
    uint8_t *dst = out_buf.data() + static_cast<size_t>(s.y_start) * width;
    if (tjDecompress2(tj, s.jpeg, s.jpeg_size, dst, static_cast<int>(width),
                      static_cast<int>(width), static_cast<int>(s.slice_height),
                      TJPF_GRAY, 0) != 0) {
      ok.store(false, std::memory_order_relaxed);
    }
  });
  return ok.load(std::memory_order_relaxed);
}

static bool DecodeToYUV(const std::vector<SliceRef> &slices, uint32_t width,
                        uint32_t height, vtp_pixel_format_t target_fmt,
                        VTPJpegWorkerPool &pool, std::vector<uint8_t> &out_buf,
                        uint32_t &out_stride, std::vector<uint8_t> &scratch) {
  int jw = 0, jh = 0, subsamp = TJSAMP_420, cs = 0;
  if (tjDecompressHeader3(pool.Handle(0), slices[0].jpeg, slices[0].jpeg_size,
                          &jw, &jh, &subsamp, &cs) != 0) {
    return false;
  }

  const int w = static_cast<int>(width);
  const int h = static_cast<int>(height);

  const bool even_dims = ((w & 1) == 0) && ((h & 1) == 0);
  if (even_dims && (subsamp == TJSAMP_420 || subsamp == TJSAMP_422)) {
    const int cw = w / 2;
    const int ch = (subsamp == TJSAMP_420) ? (h / 2) : h;
    const int y_plane_bytes = w * h;
    const int c_plane_bytes = cw * ch;

    if ((target_fmt == VTP_FORMAT_I420 && subsamp == TJSAMP_420) ||
        (target_fmt == VTP_FORMAT_YV12 && subsamp == TJSAMP_420) ||
        (target_fmt == VTP_FORMAT_I422 && subsamp == TJSAMP_422)) {
      out_stride = width;
      out_buf.resize(static_cast<size_t>(y_plane_bytes) +
                     static_cast<size_t>(c_plane_bytes) * 2);

      uint8_t *y_base = out_buf.data();
      uint8_t *u_base = (target_fmt == VTP_FORMAT_YV12)
                            ? (y_base + y_plane_bytes + c_plane_bytes)
                            : (y_base + y_plane_bytes);
      uint8_t *v_base = (target_fmt == VTP_FORMAT_YV12)
                            ? (y_base + y_plane_bytes)
                            : (y_base + y_plane_bytes + c_plane_bytes);

      std::atomic<bool> ok{true};
      pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
        const SliceRef &s = slices[idx];
        int cy = (subsamp == TJSAMP_420) ? (s.y_start / 2) : s.y_start;
        unsigned char *planes[3] = {
            y_base + static_cast<size_t>(s.y_start) * w,
            u_base + static_cast<size_t>(cy) * cw,
            v_base + static_cast<size_t>(cy) * cw};
        int strides[3] = {w, cw, cw};
        if (tjDecompressToYUVPlanes(tj, s.jpeg, s.jpeg_size, planes, w, strides,
                                    static_cast<int>(s.slice_height), 0) != 0) {
          ok.store(false, std::memory_order_relaxed);
        }
      });
      return ok.load(std::memory_order_relaxed);
    }

    const size_t y_off = 0;
    const size_t u_off = static_cast<size_t>(y_plane_bytes);
    const size_t v_off = u_off + static_cast<size_t>(c_plane_bytes);
    scratch.resize(v_off + static_cast<size_t>(c_plane_bytes));

    uint8_t *y_base = scratch.data() + y_off;
    uint8_t *u_base = scratch.data() + u_off;
    uint8_t *v_base = scratch.data() + v_off;

    bool direct_y_to_out = (target_fmt == VTP_FORMAT_NV12);
    if (direct_y_to_out) {
      out_stride = width;
      out_buf.resize(static_cast<size_t>(w) * h +
                     static_cast<size_t>(w) * (h / 2));
      y_base = out_buf.data();
    }

    std::atomic<bool> ok{true};
    pool.RunParallel(slices.size(), [&](size_t idx, tjhandle tj) {
      const SliceRef &s = slices[idx];
      int cy = (subsamp == TJSAMP_420) ? (s.y_start / 2) : s.y_start;
      unsigned char *planes[3] = {
          y_base + static_cast<size_t>(s.y_start) * w,
          u_base + static_cast<size_t>(cy) * cw,
          v_base + static_cast<size_t>(cy) * cw};
      int strides[3] = {w, cw, cw};
      if (tjDecompressToYUVPlanes(tj, s.jpeg, s.jpeg_size, planes, w, strides,
                                  static_cast<int>(s.slice_height), 0) != 0) {
        ok.store(false, std::memory_order_relaxed);
      }
    });
    if (!ok.load(std::memory_order_relaxed))
      return false;

    if (target_fmt == VTP_FORMAT_UYVY || target_fmt == VTP_FORMAT_YUY2) {
      out_stride = width * 2;
      out_buf.resize(static_cast<size_t>(out_stride) * height);
      const uint8_t *u_422 = u_base;
      const uint8_t *v_422 = v_base;
      std::vector<uint8_t> tmp_422;
      if (subsamp == TJSAMP_420) {
        size_t c422 = static_cast<size_t>(cw) * h;
        tmp_422.resize(c422 * 2);
        simd::ResampleChroma420To422(u_base, cw, tmp_422.data(), cw, cw, h);
        simd::ResampleChroma420To422(v_base, cw, tmp_422.data() + c422, cw, cw,
                                     h);
        u_422 = tmp_422.data();
        v_422 = tmp_422.data() + c422;
      }
      if (target_fmt == VTP_FORMAT_UYVY) {
        simd::PackYUV422PlanesToUYVY(y_base, w, u_422, cw, v_422, cw,
                                     out_buf.data(), static_cast<int>(out_stride),
                                     w, h);
      } else {
        simd::PackYUV422PlanesToYUY2(y_base, w, u_422, cw, v_422, cw,
                                     out_buf.data(), static_cast<int>(out_stride),
                                     w, h);
      }
      return true;
    }

    if (target_fmt == VTP_FORMAT_NV12) {
      uint8_t *uv_dst = out_buf.data() + static_cast<size_t>(w) * h;
      if (subsamp == TJSAMP_420) {
        simd::InterleavePlanesToNV12UV(u_base, cw, v_base, cw, uv_dst, w, cw,
                                       h / 2);
      } else {
        size_t c420 = static_cast<size_t>(cw) * (h / 2);
        std::vector<uint8_t> tmp_420(c420 * 2);
        simd::ResampleChroma422To420(u_base, cw, tmp_420.data(), cw, cw, h);
        simd::ResampleChroma422To420(v_base, cw, tmp_420.data() + c420, cw, cw,
                                     h);
        simd::InterleavePlanesToNV12UV(tmp_420.data(), cw,
                                       tmp_420.data() + c420, cw, uv_dst, w, cw,
                                       h / 2);
      }
      return true;
    }

    if (target_fmt == VTP_FORMAT_I420 || target_fmt == VTP_FORMAT_YV12) {
      int c420 = cw * (h / 2);
      out_stride = width;
      out_buf.resize(static_cast<size_t>(y_plane_bytes) +
                     static_cast<size_t>(c420) * 2);
      std::memcpy(out_buf.data(), y_base, static_cast<size_t>(y_plane_bytes));
      uint8_t *u_dst = (target_fmt == VTP_FORMAT_YV12)
                           ? (out_buf.data() + y_plane_bytes + c420)
                           : (out_buf.data() + y_plane_bytes);
      uint8_t *v_dst = (target_fmt == VTP_FORMAT_YV12)
                           ? (out_buf.data() + y_plane_bytes)
                           : (out_buf.data() + y_plane_bytes + c420);
      simd::ResampleChroma422To420(u_base, cw, u_dst, cw, cw, h);
      simd::ResampleChroma422To420(v_base, cw, v_dst, cw, cw, h);
      return true;
    }

    if (target_fmt == VTP_FORMAT_I422) {
      int c422 = cw * h;
      out_stride = width;
      out_buf.resize(static_cast<size_t>(y_plane_bytes) +
                     static_cast<size_t>(c422) * 2);
      std::memcpy(out_buf.data(), y_base, static_cast<size_t>(y_plane_bytes));
      simd::ResampleChroma420To422(u_base, cw, out_buf.data() + y_plane_bytes,
                                   cw, cw, h);
      simd::ResampleChroma420To422(
          v_base, cw, out_buf.data() + y_plane_bytes + c422, cw, cw, h);
      return true;
    }
  }

  uint32_t rgb_stride = 0;
  std::vector<uint8_t> rgb_tmp;
  if (!DecodeToPackedRGB(slices, width, height, VTP_FORMAT_RGB, pool, rgb_tmp,
                         rgb_stride)) {
    return false;
  }

  const int target_subsamp =
      (target_fmt == VTP_FORMAT_I422 || target_fmt == VTP_FORMAT_UYVY ||
       target_fmt == VTP_FORMAT_YUY2)
          ? TJSAMP_422
          : TJSAMP_420;
  const int y_w = tjPlaneWidth(0, w, target_subsamp);
  const int y_h = tjPlaneHeight(0, h, target_subsamp);
  const int c_w = tjPlaneWidth(1, w, target_subsamp);
  const int c_h = tjPlaneHeight(1, h, target_subsamp);
  const size_t y_sz = static_cast<size_t>(y_w) * y_h;
  const size_t c_sz = static_cast<size_t>(c_w) * c_h;

  scratch.resize(y_sz + c_sz * 2);
  unsigned char *planes[3] = {scratch.data(), scratch.data() + y_sz,
                              scratch.data() + y_sz + c_sz};
  int strides[3] = {y_w, c_w, c_w};
  if (tjEncodeYUVPlanes(pool.Handle(0), rgb_tmp.data(), w,
                        static_cast<int>(rgb_stride), h, TJPF_RGB, planes,
                        strides, target_subsamp, 0) != 0) {
    return false;
  }

  if (target_fmt == VTP_FORMAT_UYVY) {
    out_stride = width * 2;
    out_buf.resize(static_cast<size_t>(out_stride) * height);
    simd::PackYUV422PlanesToUYVY(planes[0], y_w, planes[1], c_w, planes[2], c_w,
                                 out_buf.data(), static_cast<int>(out_stride),
                                 w, h);
  } else if (target_fmt == VTP_FORMAT_YUY2) {
    out_stride = width * 2;
    out_buf.resize(static_cast<size_t>(out_stride) * height);
    simd::PackYUV422PlanesToYUY2(planes[0], y_w, planes[1], c_w, planes[2], c_w,
                                 out_buf.data(), static_cast<int>(out_stride),
                                 w, h);
  } else if (target_fmt == VTP_FORMAT_NV12) {
    out_stride = width;
    out_buf.resize(y_sz + static_cast<size_t>(w) * c_h);
    std::memcpy(out_buf.data(), planes[0], y_sz);
    simd::InterleavePlanesToNV12UV(planes[1], c_w, planes[2], c_w,
                                   out_buf.data() + y_sz, w, c_w, c_h);
  } else if (target_fmt == VTP_FORMAT_YV12) {
    out_stride = width;
    out_buf.resize(y_sz + c_sz * 2);
    std::memcpy(out_buf.data(), planes[0], y_sz);
    std::memcpy(out_buf.data() + y_sz, planes[2], c_sz);
    std::memcpy(out_buf.data() + y_sz + c_sz, planes[1], c_sz);
  } else {
    out_stride = width;
    out_buf = scratch;
  }
  return true;
}

bool DecodeVideoPacketPayload(const VTPPacketHeader &pkt_hdr,
                              const uint8_t *payload, size_t payload_len,
                              int preferred_output_format,
                              VTPJpegWorkerPool &pool, DecodedVideoResult &out,
                              std::vector<uint8_t> &alpha_scratch,
                              std::vector<uint8_t> &yuv_scratch) {
  if (!payload || payload_len < sizeof(VTPFrameHeader))
    return false;

  VTPFrameHeader frame_hdr;
  std::memcpy(&frame_hdr, payload, sizeof(frame_hdr));
  if (frame_hdr.magic != VTP_MAGIC || frame_hdr.width == 0 ||
      frame_hdr.height == 0 || frame_hdr.width > VTP_MAX_FRAME_DIM ||
      frame_hdr.height > VTP_MAX_FRAME_DIM ||
      frame_hdr.fill_size > VTP_MAX_VIDEO_PAYLOAD ||
      frame_hdr.key_size > VTP_MAX_VIDEO_PAYLOAD) {
    return false;
  }

  size_t off = sizeof(VTPFrameHeader);
  out.metadata.timecode_ns = 0;
  out.metadata.color_space = VTP_COLOR_BT709;
  out.metadata.scan_mode = VTP_SCAN_PROGRESSIVE;
  out.metadata.aspect_ratio =
      static_cast<float>(frame_hdr.width) / static_cast<float>(frame_hdr.height);
  out.metadata.full_range = true;

  if (pkt_hdr.reserved[0] & VTP_PKT_FLAG_EXT_META) {
    if (off + sizeof(VTPFrameExtMeta) > payload_len)
      return false;
    VTPFrameExtMeta ext;
    std::memcpy(&ext, payload + off, sizeof(ext));
    off += sizeof(ext);
    out.metadata.timecode_ns = ext.timecode_ns;
    out.metadata.color_space = static_cast<vtp_color_space_t>(ext.color_space);
    out.metadata.scan_mode = static_cast<vtp_scan_mode_t>(ext.scan_mode);
    out.metadata.aspect_ratio = ext.aspect_ratio;
    out.metadata.full_range = ext.full_range != 0;
  }

  const size_t key_sz = frame_hdr.has_alpha ? frame_hdr.key_size : 0;
  if (off + frame_hdr.fill_size + key_sz > payload_len)
    return false;

  const uint8_t *fill_ptr = payload + off;
  const uint8_t *key_ptr = fill_ptr + frame_hdr.fill_size;
  const bool is_sliced = (pkt_hdr.reserved[0] & VTP_PKT_FLAG_SLICED) != 0;

  std::vector<SliceRef> fill_slices;
  if (is_sliced) {
    if (!ParseSlices(fill_ptr, frame_hdr.fill_size, frame_hdr.height,
                     fill_slices)) {
      return false;
    }
  } else {
    fill_slices.push_back(
        {0, static_cast<uint16_t>(frame_hdr.height), fill_ptr, frame_hdr.fill_size});
  }

  vtp_pixel_format_t target_fmt =
      (preferred_output_format >= 0)
          ? static_cast<vtp_pixel_format_t>(preferred_output_format)
          : static_cast<vtp_pixel_format_t>(frame_hdr.pixel_format);

  bool ok = false;
  if (IsYUVPixelFormat(target_fmt)) {
    ok = DecodeToYUV(fill_slices, frame_hdr.width, frame_hdr.height, target_fmt,
                     pool, out.data, out.stride, yuv_scratch);
    out.has_alpha = false;
  } else {
    ok = DecodeToPackedRGB(fill_slices, frame_hdr.width, frame_hdr.height,
                           target_fmt, pool, out.data, out.stride);
    if (ok && frame_hdr.has_alpha &&
        (target_fmt == VTP_FORMAT_RGBA || target_fmt == VTP_FORMAT_BGRA) &&
        key_sz > 0) {
      std::vector<SliceRef> key_slices;
      bool key_ok = false;
      if (is_sliced) {
        key_ok = ParseSlices(key_ptr, key_sz, frame_hdr.height, key_slices);
      } else {
        key_slices.push_back({0, static_cast<uint16_t>(frame_hdr.height),
                              key_ptr, static_cast<uint32_t>(key_sz)});
        key_ok = true;
      }
      if (key_ok && DecodeToGray(key_slices, frame_hdr.width, frame_hdr.height,
                                 pool, alpha_scratch)) {
        simd::InjectAlphaStrided(out.data.data(), static_cast<int>(out.stride),
                                 alpha_scratch.data(),
                                 static_cast<int>(frame_hdr.width),
                                 static_cast<int>(frame_hdr.height));
        out.has_alpha = true;
      } else {
        out.has_alpha = false;
      }
    } else {
      out.has_alpha = false;
    }
  }

  if (!ok)
    return false;

  out.width = frame_hdr.width;
  out.height = frame_hdr.height;
  out.format = target_fmt;
  out.timestamp_ns = frame_hdr.timestamp_ns;
  out.frame_index = frame_hdr.frame_index;
  out.is_proxy = (pkt_hdr.reserved[0] & VTP_PKT_FLAG_PROXY) != 0;
  return true;
}

} // namespace vtp
