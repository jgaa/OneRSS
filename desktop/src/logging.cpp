#include "logging.h"

#include <QFileInfo>

#include <iostream>
#include <memory>

namespace onerss::desktop {

void LoggingController::ensureDefaults(QSettings &settings) const {
#ifndef NDEBUG
  constexpr int default_file_level = kTraceLevel;
#else
  constexpr int default_file_level = kInfoLevel;
#endif

  if (!settings.contains(QStringLiteral("logging/applevel"))) {
    settings.setValue(QStringLiteral("logging/applevel"), kInfoLevel);
  }
  if (!settings.contains(QStringLiteral("logging/filelevel"))) {
    settings.setValue(QStringLiteral("logging/filelevel"), default_file_level);
  }
  if (!settings.contains(QStringLiteral("logging/path"))) {
    settings.setValue(QStringLiteral("logging/path"), QStringLiteral("/tmp/onerss-desktop.log"));
  }
  if (!settings.contains(QStringLiteral("logging/prune"))) {
    settings.setValue(QStringLiteral("logging/prune"), true);
  }
}

void LoggingController::initialize() const {
  QSettings settings;
  ensureDefaults(settings);
  settings.sync();

  if (const auto app_level = settings.value(QStringLiteral("logging/applevel"), kInfoLevel).toInt();
      app_level > kDisabledLevel) {
    logfault::LogManager::Instance().AddHandler(
      std::make_unique<logfault::StreamHandler>(std::clog, static_cast<logfault::LogLevel>(app_level)));
  }

  if (const auto file_level = settings.value(QStringLiteral("logging/filelevel"), kDisabledLevel).toInt();
      file_level > kDisabledLevel) {
    logfault::LogManager::Instance().AddHandler(std::make_unique<logfault::StreamHandler>(
      settings.value(QStringLiteral("logging/path")).toString().toStdString(),
      static_cast<logfault::LogLevel>(file_level),
      settings.value(QStringLiteral("logging/prune")).toBool()));
  }
}

QString LoggingController::settingsFilePath() const {
  return QFileInfo(QSettings{}.fileName()).absoluteFilePath();
}

}  // namespace onerss::desktop
