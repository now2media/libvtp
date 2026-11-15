#pragma once

#include "VTPCommon.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace vtp {

class VTPDiscoveryAdvertiser {
public:
  VTPDiscoveryAdvertiser(const std::string &name, uint16_t port, int width,
                         int height, int fps, bool enable_alpha,
                         bool enable_audio, bool is_udp = false,
                         bool has_proxy = false);
  ~VTPDiscoveryAdvertiser();

  void Start();
  void Stop();
  void UpdateStreamInfo(int width, int height, int fps);

private:
  void Run();

  std::string name_;
  uint16_t port_;
  std::mutex info_mutex_;
  int width_;
  int height_;
  int fps_;
  bool enable_alpha_;
  bool enable_audio_;
  bool is_udp_;
  bool has_proxy_;
  std::thread thread_;
  std::atomic<bool> running_{false};
};

class VTPDiscoveryListener {
public:
  VTPDiscoveryListener();
  ~VTPDiscoveryListener();

  void Start();
  void Stop();

  int GetSources(VTPSource *out_sources, int max_sources);
  bool FindSource(const char *name, VTPSource *out_source,
                  uint8_t *out_flags = nullptr);

private:
  struct DiscoveredSource {
    VTPSource source;
    uint8_t flags = 0;
    std::chrono::steady_clock::time_point last_seen;
  };

  void Run();
  void CleanStaleSourcesLocked();

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  std::vector<DiscoveredSource> sources_;
};

} // namespace vtp
