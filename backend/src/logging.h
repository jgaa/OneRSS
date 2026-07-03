#pragma once

#define LOGFAULT_USE_TID_AS_NAME 1

#include <optional>
#include <string_view>

#include <logfault/logfault.h>

namespace onerss::backend::logging {

void configure(std::string_view file_path,
               std::optional<logfault::LogLevel> console_level,
               logfault::LogLevel file_level,
               bool truncate_file);

logfault::LogLevel parseLogLevel(std::string_view name,
                                 logfault::LogLevel fallback = logfault::LogLevel::INFO) noexcept;

}  // namespace onerss::backend::logging

#define LOG_ERROR LFLOG_ERROR
#define LOG_WARN LFLOG_WARN
#define LOG_NOTICE LFLOG_NOTICE
#define LOG_INFO LFLOG_INFO
#define LOG_DEBUG LFLOG_DEBUG
#define LOG_TRACE LFLOG_TRACE
