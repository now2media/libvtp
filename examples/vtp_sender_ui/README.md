# VTP Sender UI Example

This is a standalone Qt6-based GUI application that allows you to stream video files over the network using the Video Transfer Protocol (VTP).

## Features
- Select any video file (MP4, MKV, AVI, etc.) to decode and stream.
- Input fields to configure:
  - Stream Name
  - Target Resolution (Width & Height)
  - Target Frame Rate (FPS)
  - Enable Alpha Channel (Key/Fill)
- Sleek modern dark mode UI.

## Dependencies
- **Qt6** (Widgets, Multimedia)
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
./vtp_sender_ui
```
