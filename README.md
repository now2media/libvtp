# libvtp

A C/C++ library for low-latency video, audio, tally, and metadata transmission over local networks.

## Features

- Real-time video and audio transmission over TCP or UDP (with optional XOR FEC)
- RGB, RGBA, BGR, BGRA, I420, YV12, NV12, UYVY, YUY2, and I422 pixel formats with Alpha Key/Fill and proxy tier support
- Multi-slice TurboJPEG encode/decode and AVX2/SSE2 alpha and YUV helpers
- PCM (S16/S24/S32/F32) and optional Opus audio (`libopus`)
- UDP multicast discovery (`239.255.42.99:9999`), direct IP connection, and auto-reconnect
- Bidirectional tally, PTZ, control commands, and frame metadata

## Requirements

- CMake (>= 3.15)
- libjpeg-turbo (`turbojpeg`)
- Asio (system headers or fetched automatically via `FetchContent`)
- libopus (optional)
- Qt6 Widgets and Multimedia (optional, for UI examples)

## How to Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

Build options:

- `-DVTP_BUILD_EXAMPLES=ON|OFF`
- `-DVTP_BUILD_TOOLS=ON|OFF`
- `-DVTP_ENABLE_OPUS=ON|OFF`

CMake / pkg-config usage:

```cmake
find_package(vtp 2.0 REQUIRED)
target_link_libraries(my_app PRIVATE vtp::vtp)
```

## Quick Start (C API)

```c
#include <vtp.h>

vtp_sender_config_t cfg;
vtp_sender_config_init(&cfg);
cfg.stream_name = "Camera-1";
cfg.width = 1920;
cfg.height = 1080;
cfg.fps = 60;

vtp_sender_t *sender = vtp_create_sender_ex(&cfg);
vtp_sender_start(sender);

vtp_sender_send_frame_ex(sender, frame_buffer, 0, 1920, 1080, VTP_FORMAT_RGBA, 0, NULL);

vtp_sender_stop(sender);
vtp_destroy_sender(sender);
```

## CLI Tools

- `vtp-discover [--watch] [--timeout <ms>]`
- `vtp-bench [--send | --recv | --bench] [--udp] [--fec] [--proxy] [--opus]`

## License
MIT
