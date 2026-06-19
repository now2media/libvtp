#pragma once

#include "VTPCommon.h"
#include <string>
#include <memory>

namespace vtp {

class VTPSender {
public:
    VTPSender(const std::string& stream_name, int width, int height, int fps, bool enable_alpha, bool enable_audio);
    ~VTPSender();

    bool Start();
    void Stop();

    bool SendFrame(const unsigned char* data, VTPPixelFormat format, uint64_t timestamp_ns);
    bool SendAudio(const unsigned char* pcm_data, int sample_count, uint32_t sample_rate, uint8_t channels, uint64_t timestamp_ns);

    void SetScaleQuality(int quality);

    uint16_t GetPort() const;
    std::string GetStreamName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vtp
