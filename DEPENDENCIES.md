# Dependencies

External dependencies are not tracked in Git. Install them via your system package manager or place third-party SDK bundles locally as noted below.

## Build Toolchain

- **CMake** >= 3.15 (>= 3.16 for examples)
- **C++ Compiler**: C++17 (`GCC`, `Clang`, or `MSVC`; C++20 for `now2sdk_example`)
- **pkg-config**: optional for `libvtp` (`libopus` lookup), required for FFmpeg-based examples

## Core Library (`libvtp`) & CLI Tools (`vtp-discover`, `vtp-bench`)

| Dependency | Requirement | Notes |
| --- | --- | --- |
| **Asio** (>= 1.30.2) | Required | Standalone header-only (`asio.hpp`). Uses system headers if found; otherwise fetched automatically during CMake configure via `FetchContent` (`https://github.com/chriskohlhoff/asio.git`, tag `asio-1-30-2`). |
| **libjpeg-turbo** (`libturbojpeg`) | Required | Provides `turbojpeg.h` and `libturbojpeg`. |
| **libopus** (`opus`) | Optional | Provides `opus/opus.h` and `libopus` (`-DVTP_ENABLE_OPUS=ON|OFF`). Falls back to uncompressed PCM if not installed. |
| **Platform Libraries** | Required | `pthread` (`Threads::Threads`) on Linux/macOS; `ws2_32` and `wsock32` on Windows. |

## Qt UI Examples (`examples/vtp_sender_ui`, `examples/vtp_receiver_ui`)

Built when `-DVTP_BUILD_EXAMPLES=ON` (or standalone inside each example folder).

- **Qt6**: `Widgets`, `Multimedia` (required for both `vtp_sender_ui` and `vtp_receiver_ui`)
- **FFmpeg**: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample` (required for `vtp_sender_ui`)

## `now2sdk` Examples (`examples/now2sdk_example/LiveDevice`, `examples/now2sdk_example/Output`)

Standalone examples; their `SDK/` folders are excluded from Git. To build them, place the vendor binaries and headers under `examples/now2sdk_example/<App>/SDK/` alongside system Qt6/Qt5 and FFmpeg dev packages:

- **now2sdk**: `libnow2sdk.so`, `header/now2sdk/*.h`
- **Qt 6 / Qt 5**: `Core`, `Gui`, `Widgets`, `OpenGL`, `OpenGLWidgets`, `Concurrent`, `Multimedia`, `DBus`
- **FFmpeg**: `libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`, `libavfilter` (plus codec libs `libdav1d`, `librav1e`, `libdvdnav` when bundling)
- **NDI SDK**: `libndi.so` and headers under `header/ndi` (when used)
- **Blackmagic DeckLink SDK**: `libDeckLinkAPI.so`, `libDeckLinkPreviewAPI.so`, and headers under `header/decklink` (when used)
- **Bundled runtime libraries** (when distributing standalone with `libnow2sdk.so`): `libSDL2-2.0.so`, `libthorvg-1.so`, `libc++.so.1`, `libc++abi.so.1`, `libvtp.so`

## Installing System Packages (Linux)

### Debian / Ubuntu

```bash
sudo apt install cmake pkg-config g++ libasio-dev libturbojpeg0-dev libopus-dev \
  qt6-base-dev qt6-multimedia-dev \
  libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev
```

### Fedora / RHEL

```bash
sudo dnf install cmake pkgconf-pkg-config gcc-c++ asio-devel turbojpeg-devel opus-devel \
  qt6-qtbase-devel qt6-qtmultimedia-devel \
  ffmpeg-free-devel
```

### Arch Linux

```bash
sudo pacman -S cmake pkgconf gcc asio libjpeg-turbo opus qt6-base qt6-multimedia ffmpeg
```
