#pragma once

#include "VTPCommon.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <turbojpeg.h>
#include <vector>

namespace vtp {

class VTPJpegWorkerPool {
public:
  VTPJpegWorkerPool(int num_threads, bool compress_mode);
  ~VTPJpegWorkerPool();

  VTPJpegWorkerPool(const VTPJpegWorkerPool &) = delete;
  VTPJpegWorkerPool &operator=(const VTPJpegWorkerPool &) = delete;

  int ThreadCount() const { return static_cast<int>(handles_.size()); }
  tjhandle Handle(size_t idx) const {
    return idx < handles_.size() ? handles_[idx] : nullptr;
  }

  void RunParallel(size_t task_count,
                   const std::function<void(size_t task_idx, tjhandle tj)> &fn);

private:
  void WorkerLoop(size_t worker_id);

  std::vector<tjhandle> handles_;
  std::vector<std::thread> workers_;
  std::mutex mutex_;
  std::condition_variable cv_start_;
  std::condition_variable cv_done_;
  bool stop_ = false;
  uint64_t generation_ = 0;
  std::atomic<size_t> next_task_{0};
  size_t total_tasks_ = 0;
  size_t active_workers_ = 0;
  const std::function<void(size_t, tjhandle)> *current_fn_ = nullptr;
};

struct DecodedVideoResult {
  std::vector<uint8_t> data;
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t stride = 0;
  vtp_pixel_format_t format = VTP_FORMAT_RGB;
  uint64_t timestamp_ns = 0;
  uint32_t frame_index = 0;
  bool has_alpha = false;
  bool is_proxy = false;
  vtp_video_metadata_t metadata{};
};

bool IsYUVPixelFormat(vtp_pixel_format_t fmt);

bool DecodeVideoPacketPayload(const VTPPacketHeader &pkt_hdr,
                              const uint8_t *payload, size_t payload_len,
                              int preferred_output_format,
                              VTPJpegWorkerPool &pool, DecodedVideoResult &out,
                              std::vector<uint8_t> &alpha_scratch,
                              std::vector<uint8_t> &yuv_scratch);

} // namespace vtp
