#include "VTPLog.h"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace vtp {

static std::atomic<int> g_log_level{static_cast<int>(VTP_LOG_WARN)};
static std::mutex g_log_mutex;
static vtp_log_callback_t g_log_cb = nullptr;
static void *g_log_user_data = nullptr;

static const char *LevelTag(vtp_log_level_t level) {
  switch (level) {
  case VTP_LOG_DEBUG:
    return "DEBUG";
  case VTP_LOG_INFO:
    return "INFO";
  case VTP_LOG_WARN:
    return "WARN";
  case VTP_LOG_ERROR:
    return "ERROR";
  default:
    return "NONE";
  }
}

void SetLogCallback(vtp_log_callback_t cb, void *user_data) {
  std::lock_guard<std::mutex> lock(g_log_mutex);
  g_log_cb = cb;
  g_log_user_data = user_data;
}

void SetLogLevel(vtp_log_level_t level) {
  g_log_level.store(static_cast<int>(level), std::memory_order_relaxed);
}

vtp_log_level_t GetLogLevel() {
  return static_cast<vtp_log_level_t>(
      g_log_level.load(std::memory_order_relaxed));
}

void Log(vtp_log_level_t level, const char *fmt, ...) {
  if (static_cast<int>(level) < g_log_level.load(std::memory_order_relaxed) ||
      level >= VTP_LOG_NONE || !fmt) {
    return;
  }

  char stack_buf[1024];
  std::string heap_buf;
  const char *msg_ptr = stack_buf;

  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  int needed = std::vsnprintf(stack_buf, sizeof(stack_buf), fmt, args);
  va_end(args);

  if (needed >= static_cast<int>(sizeof(stack_buf))) {
    heap_buf.resize(static_cast<size_t>(needed) + 1);
    std::vsnprintf(&heap_buf[0], heap_buf.size(), fmt, args_copy);
    msg_ptr = heap_buf.c_str();
  }
  va_end(args_copy);

  vtp_log_callback_t cb = nullptr;
  void *user_data = nullptr;
  {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    cb = g_log_cb;
    user_data = g_log_user_data;
  }

  if (cb) {
    cb(level, msg_ptr, user_data);
  } else {
    std::fprintf(stderr, "[VTP][%s] %s\n", LevelTag(level), msg_ptr);
  }
}

} // namespace vtp
