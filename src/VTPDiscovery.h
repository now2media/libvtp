#pragma once

#include "VTPCommon.h"
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

namespace vtp {

class VTPDiscoveryAdvertiser {
public:
    VTPDiscoveryAdvertiser(const std::string& name, uint16_t port, int width, int height, int fps, bool enable_alpha, bool enable_audio);
    ~VTPDiscoveryAdvertiser();

    void Start();
    void Stop();

private:
    void Run();

    std::string name_;
    uint16_t port_;
    int width_;
    int height_;
    int fps_;
    bool enable_alpha_;
    bool enable_audio_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

class VTPDiscoveryListener {
public:
    VTPDiscoveryListener();
    ~VTPDiscoveryListener();

    void Start();
    void Stop();

    int GetSources(VTPSource* out_sources, int max_sources);

private:
    struct DiscoveredSource {
        VTPSource source;
        std::chrono::steady_clock::time_point last_seen;
    };

    void Run();
    void CleanStaleSources();

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::vector<DiscoveredSource> sources_;
};

} // namespace vtp
