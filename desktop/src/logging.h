#pragma once

#define LOGFAULT_USE_TID_AS_NAME 1

#include <QSettings>
#include <QString>

#include <logfault/logfault.h>

namespace onerss::desktop {

class LoggingController final {
 public:
  static constexpr int kDisabledLevel = 0;
  static constexpr int kInfoLevel = 4;
  static constexpr int kDebugLevel = 5;
  static constexpr int kTraceLevel = 6;

  void initialize() const;
  void ensureDefaults(QSettings &settings) const;
  [[nodiscard]] QString settingsFilePath() const;
};

}  // namespace onerss::desktop

#define LOG_ERROR LFLOG_ERROR
#define LOG_WARN LFLOG_WARN
#define LOG_NOTICE LFLOG_NOTICE
#define LOG_INFO LFLOG_INFO
#define LOG_DEBUG LFLOG_DEBUG
#define LOG_TRACE LFLOG_TRACE
