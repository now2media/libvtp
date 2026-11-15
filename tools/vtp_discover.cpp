#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vtp.h>

static std::atomic<bool> g_running{true};

static void HandleSignal(int) { g_running.store(false); }

static void PrintSources(vtp_listener_t *listener) {
  vtp_source_t sources[VTP_MAX_SOURCES];
  int count = vtp_listener_get_sources(listener, sources, VTP_MAX_SOURCES);
  std::printf("%-24s %-16s %-7s %-12s %-5s %-5s %-5s\n", "NAME", "IP",
              "PORT", "RESOLUTION", "FPS", "ALPHA", "AUDIO");
  std::printf("----------------------------------------------------------------"
              "---------\n");
  for (int i = 0; i < count; ++i) {
    const auto &s = sources[i];
    char res[32];
    std::snprintf(res, sizeof(res), "%ux%u", s.width, s.height);
    std::printf("%-24s %-16s %-7u %-12s %-5u %-5s %-5s\n", s.name, s.ip,
                s.port, res, s.fps, s.has_alpha ? "yes" : "no",
                s.has_audio ? "yes" : "no");
  }
  if (count == 0) {
    std::printf("(no active VTP sources found)\n");
  }
}

int main(int argc, char **argv) {
  bool watch = false;
  int timeout_ms = 2000;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--watch") == 0 ||
        std::strcmp(argv[i], "-w") == 0) {
      watch = true;
    } else if ((std::strcmp(argv[i], "--timeout") == 0 ||
                std::strcmp(argv[i], "-t") == 0) &&
               i + 1 < argc) {
      timeout_ms = std::atoi(argv[++i]);
      if (timeout_ms < 100)
        timeout_ms = 100;
    } else if (std::strcmp(argv[i], "--help") == 0 ||
               std::strcmp(argv[i], "-h") == 0) {
      std::printf("Usage: vtp-discover [--watch | -w] [--timeout <ms>]\n");
      return 0;
    }
  }

  std::signal(SIGINT, HandleSignal);
  vtp_set_log_level(VTP_LOG_WARN);

  vtp_listener_t *listener = vtp_create_listener();
  vtp_listener_start(listener);

  if (!watch) {
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms));
    PrintSources(listener);
  } else {
    while (g_running.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      if (!g_running.load())
        break;
      std::printf("\033[2J\033[H");
      PrintSources(listener);
      std::fflush(stdout);
    }
  }

  vtp_listener_stop(listener);
  vtp_destroy_listener(listener);
  return 0;
}
