# VTP (Video Transmission Protocol)

**VTP (Video Transmission Protocol)** is a lightweight, ultra-high-performance, and broadcast-grade C++ SDK designed for real-time video and audio streaming over local networks (LAN/WiFi). With an optimized compression pipeline and low-overhead networking, VTP delivers sub-frame latency, making it a perfect, royalty-free alternative for live production workflows.

---

## ⚡ Key Highlights & Performance

* **Ultra-Low Latency (Sub-10ms)**: Engineered specifically for interactive live production. End-to-end network transmission and decompression latency is **under 10ms** on standard local networks.
* **Broadcast-Grade Quality**: Features **4:2:2 chroma subsampling** via TurboJPEG to prevent color bleeding on text/graphics boundaries, coupled with high-quality **Bicubic scaling** on the sender and **Smooth Pixmap Transform** (Anti-aliased) rendering on the receiver.
* **Production-Ready Resolutions**: Optimized to stream high-bitrate video formats smoothly:
  * **4K UHD (3840x2160) (1Gbps optimized) ** @ 25p / 30p
  * **2K QHD (2560x1440) (1Gbps optimized) ** @ 25p / 30p / 50p / 60p
  * **1080p Full HD (1920x1080) (100Mbps optimized)** @ 25p / 30p / 50p / 60p / (90p / 100p /  120p 1Gbps optimized)
  * **720p HD (1280x720) (WiFi optimized)** @ 25p / 30p / 50p / 60p / 90p / 100p / 120p 
* **Zero Dependency Leaks & Symbol Protection**: Internal components (such as **Asio** and **libjpeg-turbo**) are entirely encapsulated using the **Pimpl Pattern**. With C++ symbol visibility restricted to `hidden` and customized linker version scripts (`vtp.map`), VTP is guaranteed to link side-by-side with heavy broadcast suites (like **NDI** and **DeckLink SDK**) without any runtime symbol clashes or memory crashes.

---

## 🚀 Features

* **Real-time Scale Quality Control**: Dynamically adjust compression/quality levels (10-100) at runtime to match network bandwidth conditions.
* **Alpha Channel Support (Key/Fill)**: Seamlessly transmit RGBA/BGRA frames with transparency for overlays and lower-thirds.
* **Embedded Audio Stream**: Synchronized transmission of raw PCM audio alongside the video feed.
* **Auto-Discovery & Manual Failover**: Automatic stream discovery via UDP Multicast, with a fallback manual connection mode (Raw IP:Port) for restricted WiFi/Enterprise networks.
* **Clean C Wrapper**: Fully compatible with any language capable of binding to standard C libraries (Python, C#, Rust, Go, etc.).

---

## 🛠️ Quick Integration

### Header (`vtp.h`)
The public API is designed to be extremely clean and namespace-isolated. 

```c
#include <vtp.h>

// Initialize a sender
vtp_sender_t* sender = vtp_create_sender("VTP-Camera-1", 1920, 1080, 50, false, true);
vtp_sender_start(sender);

// Dynamically update stream quality (Scale Quality)
vtp_sender_set_scale_quality(sender, 90);

// Send RGB/RGBA frame
vtp_sender_send_frame(sender, raw_rgb_buffer, VTP_FORMAT_RGB, timestamp_ns);
```

### Building from Source

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```
