# VTP Receiver UI Example

This is a standalone Qt6-based GUI application that discovers VTP streams on the local network, connects to them, and renders the video stream in real-time.

## Features
- Auto-discovers active VTP streams on the network.
- Combobox selection listing active VTP sources with details (Resolution, FPS, Alpha support).
- Refresh button to manually trigger a network scan.
- GPU/CPU frame renderer showing the live stream.
- Status labels showing stream parameters (actual FPS, latency in milliseconds, format details).
- Sleek modern dark mode UI.

## Dependencies
- **Qt6** (Widgets)
- **libjpeg-turbo** (TurboJPEG)
- **VTP SDK** (`libvtp.so` and `vtp.h`)

## How to Build (Standalone)
If you zip this folder and move it elsewhere, ensure you copy `vtp.h` and `libvtp.so` and place them appropriately, or configure CMake paths.

Run the following commands in this directory:
```bash
mkdir build && cd build
cmake ..
make
```

## How to Run
Ensure `libvtp.so` is in the same directory as the executable, or set the library path:
```bash
export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH
./vtp_receiver_ui
```
