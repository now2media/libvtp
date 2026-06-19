Markdown
# 🚀 LIBVTP (Video Transmission Protocol)

**VTP (Video Transmission Protocol)** is an ultra-high-performance, lightweight, and broadcast-grade open-core C++ SDK engineered for real-time video and audio streaming over local networks (LAN/WiFi). 

By rewriting the rules of media plumbing, VTP completely bypasses the heavy abstraction layers of traditional protocols. Armed with a zero-copy SIMD compression pipeline and an asynchronous network engine, it delivers **sub-10ms end-to-end latency**, serving as the ultimate, royalty-free, open-source alternative to proprietary solutions like NDI®.

---

## ⚡ Performance & Production-Grade Quality

* **Sub-10ms Latency (True Real-Time):** Specifically architected for interactive live workflows. Transmission, synchronization, and decompression take less than 10 milliseconds over standard local networks.

* **Studio-Grade 4:2:2 Color Accuracy:** Features native **4:2:2 chroma subsampling** powered by TurboJPEG SIMD acceleration. This eliminates color bleeding and edge pixelation, making it flawless for **Chroma Key (Green Screen)** isolation and crisp **CG/Character Generator** text boundaries.

* **Pro-Res Scaling & Rendering:** Incorporates high-fidelity **Bicubic scaling** on the sender and anti-aliased **Smooth Pixmap Transform** rendering on the receiver.

* **Engineered for High-Bitrate Pipelines:**
  * **4K UHD (3840x2160):** Smooth 25p / 30p streaming (Optimized for 1Gbps pipelines).
  * **2K QHD (2560x1440):** Fluid 25p / 30p / 50p / 60p feeds.
  * **1080p Full HD:** High-frame-rate 25p to 120p streaming (100Mbps to 1Gbps auto-negotiated).
  * **720p HD:** Ultra-optimized 25p to 120p modes (Fine-tuned for unstable WiFi links).

---

## 🔒 Enterprise Shield: Zero Symbol Collisions

VTP is designed to live inside heavy, mission-critical broadcast suites without breaking. 

Internal third-party components (such as **Asio** and **libjpeg-turbo**) are entirely encapsulated behind the **Pimpl Pattern (Pointer to Implementation)**. Combined with strict C++ symbol visibility (`hidden`) and a custom linker version script (`vtp.map`), VTP guarantees **zero global runtime symbol clashes, zero header pollution, and absolute memory isolation** when linked side-by-side with heavy SDKs like **NDI®** or **Blackmagic DeckLink**.

---

## 🛠️ Features

* **Dynamic Scale Quality Control:** Instantly adjust compression/quality levels (10-100) at runtime to adapt to real-time network bandwidth fluctuations without dropping frames.
* **Alpha Channel Support (Key/Fill):** Native transmission of RGBA/BGRA frames with transparency for overlays, stingers, and lower-thirds.
* **Lip-Sync Embedded Audio:** Synchronized, microsecond-accurate transmission of raw PCM audio coupled directly with the video payload.
* **Auto-Discovery & Manual Failover:** Seamless stream discovery via UDP Multicast, with a robust manual connection fallback (Raw IP:Port) to bypass restrictive corporate firewalls and enterprise WiFi isolation.
* **Clean C Binding Layer:** Exposes a zero-overhead pure C API, making it instantly compatible with any language that supports foreign function interfaces (**Python, C#, Rust, Go, Node.js**).

---

## 💡 The Power of Simplicity: VTP Core vs. now2sdk Framework

While `libvtp` gives you the raw, high-performance socket and compression infrastructure, managing manual frame loops, memory alignments, and device pooling can still take up to **500+ lines of complex C++ boilerplate**.

If you want to bypass the plumbing and build production-ready streaming setups in seconds, VTP natively integrates with **now2sdk**—the ultimate free, object-based broadcast framework for Linux.

### Look at the Contrast:

```cpp
// Traditional approach: 500+ lines of socket tracking, FFmpeg bindings, and thread synchronization.
// VS.
// The now2sdk Framework: Professional broadcast automation in just 5 lines of code!

#include <now2sdk/LFile.h>
#include <now2sdk/LOutput.h>

int main() {
    LFile* m_lFile     = new LFile("live_studio_feed.mp4");
    LOutput* m_lOutput = new LOutput();
    
    m_lFile->setProps("vtp_mode", "sender");
    m_lOutput->setSource(m_lFile);
    
    m_lFile->play(); // Studio-grade VTP 4:2:2 stream is now blazing across your network!
}

```

What is now2sdk?
now2sdk is a free broadcast framework that encapsulates complex media logic into clean, reusable objects (LLive, LFile, LOutput, LMixer).
Learn more and download the free binaries at: sdk.now2media.com

⚡ Quick Integration
Pure C API (vtp.h)
C
```cpp
#include <vtp.h>

// 1. Initialize a broadcast sender node
vtp_sender_t* sender = vtp_create_sender("Studio-Cam-01", 1920, 1080, 50, false, true);
vtp_sender_start(sender);

// 2. Adjust bandwidth compression profile on the fly
vtp_sender_set_scale_quality(sender, 90);

// 3. Inject raw RGB/RGBA frame with nanosecond precision timestamps
vtp_sender_send_frame(sender, raw_rgb_buffer, VTP_FORMAT_RGB, timestamp_ns);

// 4. Clean up resources
vtp_sender_stop(sender);
vtp_destroy_sender(sender);

```

Compiling the Core
```cpp
Bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release

```

📄 License
libvtp Core is open-source software licensed under the MIT License. Advanced production nodes and reji automation modules are powered by the now2sdk ecosystem.

**Keywords:** `vtp`, `broadcast-sdk`, `video-transport`, `linux-broadcast`, `422-chroma`, `low-latency`, `libjpeg-turbo`, `ndi-alternative`, `srt-alternative`, `now2sdk`