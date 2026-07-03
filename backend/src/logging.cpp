#include "logging.h"

#include <iostream>
#include <memory>
#include <string>

namespace onerss::backend::logging {

void configure(const std::string_view file_path,
               const std::optional<logfault::LogLevel> console_level,
               const logfault::LogLevel file_level,
               const bool truncate_file) {
  auto &manager = logfault::LogManager::Instance();
  manager.AddHandler(
    std::make_unique<logfault::StreamHandler>(std::string{file_path}, file_level, truncate_file));

  if (console_level.has_value()) {
    manager.AddHandler(std::make_unique<logfault::StreamHandler>(std::clog, *console_level));
  }
}

logfault::LogLevel parseLogLevel(const std::string_view name,
                                 const logfault::LogLevel fallback) noexcept {
  if (name == "trace") {
    return logfault::LogLevel::TRACE;
  }
  if (name == "debug") {
    return logfault::LogLevel::DEBUGGING;
  }
  if (name == "info") {
    return logfault::LogLevel::INFO;
  }
  if (name == "notice") {
    return logfault::LogLevel::NOTICE;
  }
  if (name == "warn") {
    return logfault::LogLevel::WARN;
  }
  if (name == "error") {
    return logfault::LogLevel::ERROR;
  }
  if (name.empty() || name == "off" || name == "false") {
    return logfault::LogLevel::DISABLED;
  }
  return fallback;
}

}  // namespace onerss::backend::logging
