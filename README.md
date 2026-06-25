# libvtp

A C++ library for low-latency video and audio transmission over local networks.

## Features

- Real-time video/audio transmission (RGB/RGBA, PCM audio)
- UDP multicast discovery or direct IP connection
- Encapsulated dependencies to avoid symbol naming clashes

## Requirements

- CMake
- libjpeg-turbo (for JPEG compression)
- Asio (header-only, included or system)

## How to Build

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

## Quick Start (C API)

```c
#include <vtp.h>

// Initialize sender
vtp_sender_t* sender = vtp_create_sender("Camera-1", 1920, 1080, 50, false, true);
vtp_sender_start(sender);

// Send frame
vtp_sender_send_frame(sender, rgb_buffer, VTP_FORMAT_RGB, timestamp_ns);

// Clean up
vtp_sender_stop(sender);
vtp_destroy_sender(sender);
```

## License
MIT