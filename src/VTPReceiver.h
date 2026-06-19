#pragma once

#include "VTPCommon.h"
#include <string>
#include <memory>

namespace vtp {

class VTPReceiver {
public:
    VTPReceiver();
    ~VTPReceiver();

    bool Connect(const std::string& ip, uint16_t port);
    void Disconnect();

    bool ReceiveFrame(VTPFrame* out_frame);
    bool ReceiveAudio(VTPAudioFrame* out_audio);
    bool IsConnected() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vtp
