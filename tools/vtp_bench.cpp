#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include <vtp.h>

static std::atomic<bool> g_running{true};

static void HandleSignal(int) { g_running.store(false); }

static uint64_t NowSteadyNs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

static vtp_pixel_format_t ParseFormat(const char *s) {
  if (!s)
    return VTP_FORMAT_RGBA;
  if (std::strcmp(s, "rgb") == 0)
    return VTP_FORMAT_RGB;
  if (std::strcmp(s, "rgba") == 0)
    return VTP_FORMAT_RGBA;
  if (std::strcmp(s, "bgr") == 0)
    return VTP_FORMAT_BGR;
  if (std::strcmp(s, "bgra") == 0)
    return VTP_FORMAT_BGRA;
  if (std::strcmp(s, "i420") == 0)
    return VTP_FORMAT_I420;
  if (std::strcmp(s, "yv12") == 0)
    return VTP_FORMAT_YV12;
  if (std::strcmp(s, "nv12") == 0)
    return VTP_FORMAT_NV12;
  if (std::strcmp(s, "uyvy") == 0)
    return VTP_FORMAT_UYVY;
  if (std::strcmp(s, "yuy2") == 0)
    return VTP_FORMAT_YUY2;
  if (std::strcmp(s, "i422") == 0)
    return VTP_FORMAT_I422;
  return VTP_FORMAT_RGBA;
}

static size_t FrameBufferBytes(int w, int h, vtp_pixel_format_t fmt) {
  switch (fmt) {
  case VTP_FORMAT_RGB:
  case VTP_FORMAT_BGR:
    return static_cast<size_t>(w) * h * 3;
  case VTP_FORMAT_RGBA:
  case VTP_FORMAT_BGRA:
    return static_cast<size_t>(w) * h * 4;
  case VTP_FORMAT_UYVY:
  case VTP_FORMAT_YUY2:
  case VTP_FORMAT_I422:
    return static_cast<size_t>(w) * h * 2;
  case VTP_FORMAT_I420:
  case VTP_FORMAT_YV12:
  case VTP_FORMAT_NV12:
    return static_cast<size_t>(w) * h * 3 / 2;
  default:
    return static_cast<size_t>(w) * h * 4;
  }
}

struct Options {
  std::string mode = "bench";
  std::string name = "VTP-Test";
  std::string ip;
  int width = 1920;
  int height = 1080;
  int fps = 60;
  int quality = 85;
  int slices = 0;
  int frames = 300;
  uint16_t port = 0;
  vtp_pixel_format_t format = VTP_FORMAT_RGBA;
  int out_format = -1;
  bool udp = false;
  bool fec = false;
  bool proxy = false;
  bool opus = false;
  bool alpha = false;
};

static int RunSender(const Options &opt) {
  vtp_sender_config_t cfg;
  vtp_sender_config_init(&cfg);
  cfg.stream_name = opt.name.c_str();
  cfg.port = opt.port;
  cfg.width = opt.width;
  cfg.height = opt.height;
  cfg.fps = opt.fps;
  cfg.quality = opt.quality;
  cfg.slice_threads = opt.slices;
  cfg.enable_alpha = opt.alpha;
  cfg.enable_audio = true;
  cfg.transport = opt.udp ? VTP_TRANSPORT_UDP : VTP_TRANSPORT_TCP;
  cfg.enable_fec = opt.fec;
  cfg.enable_proxy = opt.proxy;
  cfg.audio_codec = opt.opus ? VTP_AUDIO_CODEC_OPUS : VTP_AUDIO_CODEC_PCM;

  vtp_sender_t *sender = vtp_create_sender_ex(&cfg);
  if (!vtp_sender_start(sender)) {
    std::fprintf(stderr, "Failed to start VTP sender\n");
    vtp_destroy_sender(sender);
    return 1;
  }

  std::printf("VTP Sender '%s' listening on port %u (%dx%d@%dfps, %s)\n",
              opt.name.c_str(), vtp_sender_get_port(sender), opt.width,
              opt.height, opt.fps, opt.udp ? "UDP" : "TCP");

  std::vector<uint8_t> video_buf(
      FrameBufferBytes(opt.width, opt.height, opt.format));
  const int samples_per_frame = 48000 / opt.fps;
  std::vector<int16_t> audio_buf(static_cast<size_t>(samples_per_frame) * 2);
  uint64_t tone_phase = 0;
  uint32_t frame_idx = 0;

  auto next_tick = std::chrono::steady_clock::now();
  const auto frame_period =
      std::chrono::microseconds(1000000 / std::max(1, opt.fps));
  auto last_log = std::chrono::steady_clock::now();

  while (g_running.load()) {
    vtp_generate_test_pattern(video_buf.data(), opt.width, opt.height, 0,
                              opt.format, frame_idx);
    uint64_t ts = NowSteadyNs();
    vtp_sender_send_frame_ex(sender, video_buf.data(), 0, opt.width, opt.height,
                             opt.format, ts, nullptr);

    vtp_generate_test_tone(audio_buf.data(), samples_per_frame, 48000, 2,
                           1000.0, &tone_phase);
    vtp_sender_send_audio_ex(sender, audio_buf.data(), samples_per_frame, 48000,
                             2, VTP_AUDIO_PCM_S16LE, ts);

    ++frame_idx;
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log)
            .count() >= 1) {
      vtp_stats_t st{};
      vtp_sender_get_stats(sender, &st);
      std::printf("[SEND] clients=%u fps=%.1f bitrate=%.2f Mbps enc=%u us "
                  "dropped=%llu\n",
                  st.connected_clients, st.fps, st.bitrate_mbps,
                  st.last_codec_time_us,
                  static_cast<unsigned long long>(st.frames_dropped));
      last_log = now;
    }

    next_tick += frame_period;
    std::this_thread::sleep_until(next_tick);
  }

  vtp_sender_stop(sender);
  vtp_destroy_sender(sender);
  return 0;
}

static int RunReceiver(const Options &opt) {
  vtp_receiver_config_t cfg;
  vtp_receiver_config_init(&cfg);
  cfg.auto_reconnect = true;
  cfg.preferred_output_format = opt.out_format;
  cfg.preferred_tier = opt.proxy ? VTP_TIER_PROXY : VTP_TIER_FULL;
  cfg.transport = opt.udp ? VTP_TRANSPORT_UDP : VTP_TRANSPORT_TCP;

  vtp_receiver_t *recv = vtp_create_receiver_ex(&cfg);
  vtp_listener_t *listener = nullptr;

  if (!opt.ip.empty()) {
    if (!vtp_receiver_connect_ex(recv, opt.ip.c_str(), opt.port, 3000)) {
      std::fprintf(stderr, "Initial connect to %s:%u failed (auto-reconnect "
                           "active)...\n",
                   opt.ip.c_str(), opt.port);
    }
  } else {
    listener = vtp_create_listener();
    vtp_listener_start(listener);
    vtp_receiver_set_auto_reconnect_name(recv, listener, opt.name.c_str(),
                                         true);
  }

  auto last_log = std::chrono::steady_clock::now();
  while (g_running.load()) {
    vtp_frame_ex_t frame{};
    if (vtp_receiver_receive_frame_ex(recv, &frame, 200)) {
      vtp_audio_frame_ex_t audio{};
      while (vtp_receiver_receive_audio_ex(recv, &audio, 0)) {
      }
    }
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::seconds>(now - last_log)
            .count() >= 1) {
      vtp_stats_t st{};
      vtp_receiver_get_stats(recv, &st);
      std::printf("[RECV] connected=%s fps=%.1f bitrate=%.2f Mbps dec=%u us "
                  "rtt=%.2f ms dropped=%llu\n",
                  vtp_receiver_is_connected(recv) ? "yes" : "no", st.fps,
                  st.bitrate_mbps, st.last_codec_time_us, st.rtt_ms,
                  static_cast<unsigned long long>(st.frames_dropped));
      last_log = now;
    }
  }

  vtp_receiver_disconnect(recv);
  vtp_destroy_receiver(recv);
  if (listener) {
    vtp_listener_stop(listener);
    vtp_destroy_listener(listener);
  }
  return 0;
}

static int RunBench(const Options &opt) {
  vtp_sender_config_t scfg;
  vtp_sender_config_init(&scfg);
  scfg.stream_name = "VTP-Bench";
  scfg.bind_ip = "127.0.0.1";
  scfg.width = opt.width;
  scfg.height = opt.height;
  scfg.fps = opt.fps;
  scfg.quality = opt.quality;
  scfg.slice_threads = opt.slices;
  scfg.enable_alpha = opt.alpha;
  scfg.transport = opt.udp ? VTP_TRANSPORT_UDP : VTP_TRANSPORT_TCP;
  scfg.enable_fec = opt.fec;
  scfg.audio_codec = opt.opus ? VTP_AUDIO_CODEC_OPUS : VTP_AUDIO_CODEC_PCM;

  vtp_sender_t *sender = vtp_create_sender_ex(&scfg);
  if (!vtp_sender_start(sender)) {
    vtp_destroy_sender(sender);
    return 1;
  }

  vtp_receiver_config_t rcfg;
  vtp_receiver_config_init(&rcfg);
  rcfg.transport = scfg.transport;
  rcfg.preferred_output_format = opt.out_format;
  vtp_receiver_t *recv = vtp_create_receiver_ex(&rcfg);
  if (!vtp_receiver_connect_ex(recv, "127.0.0.1", vtp_sender_get_port(sender),
                               2000)) {
    std::fprintf(stderr, "Loopback connect failed\n");
    vtp_destroy_receiver(recv);
    vtp_sender_stop(sender);
    vtp_destroy_sender(sender);
    return 1;
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<uint8_t> video_buf(
      FrameBufferBytes(opt.width, opt.height, opt.format));
  vtp_generate_test_pattern(video_buf.data(), opt.width, opt.height, 0,
                            opt.format, 0);

  double total_latency_ms = 0.0;
  int received = 0;

  std::thread rx_thread([&]() {
    vtp_frame_ex_t frame{};
    while (received < opt.frames && g_running.load()) {
      if (vtp_receiver_receive_frame_ex(recv, &frame, 1000)) {
        uint64_t now = NowSteadyNs();
        if (now >= frame.timestamp_ns) {
          total_latency_ms += static_cast<double>(now - frame.timestamp_ns) / 1e6;
        }
        ++received;
      } else {
        break;
      }
    }
  });

  const auto period =
      std::chrono::microseconds(1000000 / std::max(1, opt.fps));
  auto next_t = std::chrono::steady_clock::now();
  for (int i = 0; i < opt.frames && g_running.load(); ++i) {
    vtp_sender_send_frame_ex(sender, video_buf.data(), 0, opt.width, opt.height,
                             opt.format, NowSteadyNs(), nullptr);
    next_t += period;
    std::this_thread::sleep_until(next_t);
  }

  rx_thread.join();

  vtp_stats_t sst{}, rst{};
  vtp_sender_get_stats(sender, &sst);
  vtp_receiver_get_stats(recv, &rst);
  double avg_lat = received > 0 ? total_latency_ms / received : 0.0;
  std::printf("Benchmark complete (%dx%d@%dfps, %s):\n"
              "  Received: %d/%d frames | Avg E2E Latency: %.2f ms\n"
              "  Encode: %u us | Decode: %u us | Bitrate: %.2f Mbps | Dropped: "
              "%llu\n",
              opt.width, opt.height, opt.fps, opt.udp ? "UDP" : "TCP", received,
              opt.frames, avg_lat, sst.last_codec_time_us,
              rst.last_codec_time_us, rst.bitrate_mbps,
              static_cast<unsigned long long>(rst.frames_dropped));

  vtp_receiver_disconnect(recv);
  vtp_destroy_receiver(recv);
  vtp_sender_stop(sender);
  vtp_destroy_sender(sender);
  return 0;
}

int main(int argc, char **argv) {
  Options opt;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--send") == 0)
      opt.mode = "send";
    else if (std::strcmp(argv[i], "--recv") == 0)
      opt.mode = "recv";
    else if (std::strcmp(argv[i], "--bench") == 0)
      opt.mode = "bench";
    else if (std::strcmp(argv[i], "--name") == 0 && i + 1 < argc)
      opt.name = argv[++i];
    else if (std::strcmp(argv[i], "--ip") == 0 && i + 1 < argc)
      opt.ip = argv[++i];
    else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
      opt.port = static_cast<uint16_t>(std::atoi(argv[++i]));
    else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
      opt.width = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
      opt.height = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
      opt.fps = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--quality") == 0 && i + 1 < argc)
      opt.quality = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--slices") == 0 && i + 1 < argc)
      opt.slices = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
      opt.frames = std::atoi(argv[++i]);
    else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc)
      opt.format = ParseFormat(argv[++i]);
    else if (std::strcmp(argv[i], "--out-format") == 0 && i + 1 < argc)
      opt.out_format = static_cast<int>(ParseFormat(argv[++i]));
    else if (std::strcmp(argv[i], "--udp") == 0)
      opt.udp = true;
    else if (std::strcmp(argv[i], "--fec") == 0)
      opt.fec = true;
    else if (std::strcmp(argv[i], "--proxy") == 0)
      opt.proxy = true;
    else if (std::strcmp(argv[i], "--opus") == 0)
      opt.opus = true;
    else if (std::strcmp(argv[i], "--alpha") == 0)
      opt.alpha = true;
    else if (std::strcmp(argv[i], "--help") == 0 ||
             std::strcmp(argv[i], "-h") == 0) {
      std::printf(
          "Usage: vtp-bench [--send | --recv | --bench] [options]\n"
          "  --name <str> --ip <ip[:port]> --port <n>\n"
          "  --width <px> --height <px> --fps <n> --quality <1-100> --slices <n>\n"
          "  --format <rgb|rgba|bgr|bgra|i420|yv12|nv12|uyvy|yuy2|i422>\n"
          "  --out-format <fmt> --frames <n>\n"
          "  --udp --fec --proxy --opus --alpha\n");
      return 0;
    }
  }

  std::signal(SIGINT, HandleSignal);
  vtp_set_log_level(VTP_LOG_WARN);

  if (opt.mode == "send")
    return RunSender(opt);
  if (opt.mode == "recv")
    return RunReceiver(opt);
  return RunBench(opt);
}
