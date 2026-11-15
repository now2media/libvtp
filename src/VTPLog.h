#pragma once

#include <vtp.h>

namespace vtp {

void SetLogCallback(vtp_log_callback_t cb, void *user_data);
void SetLogLevel(vtp_log_level_t level);
vtp_log_level_t GetLogLevel();

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void Log(vtp_log_level_t level, const char *fmt, ...);

} // namespace vtp

#define VTP_LOG_DEBUG(...)                                                     \
  do {                                                                         \
    if (::vtp::GetLogLevel() <= VTP_LOG_DEBUG)                                 \
      ::vtp::Log(VTP_LOG_DEBUG, __VA_ARGS__);                                  \
  } while (0)

#define VTP_LOG_INFO(...)                                                      \
  do {                                                                         \
    if (::vtp::GetLogLevel() <= VTP_LOG_INFO)                                  \
      ::vtp::Log(VTP_LOG_INFO, __VA_ARGS__);                                   \
  } while (0)

#define VTP_LOG_WARN(...)                                                      \
  do {                                                                         \
    if (::vtp::GetLogLevel() <= VTP_LOG_WARN)                                  \
      ::vtp::Log(VTP_LOG_WARN, __VA_ARGS__);                                   \
  } while (0)

#define VTP_LOG_ERROR(...)                                                     \
  do {                                                                         \
    if (::vtp::GetLogLevel() <= VTP_LOG_ERROR)                                 \
      ::vtp::Log(VTP_LOG_ERROR, __VA_ARGS__);                                  \
  } while (0)
