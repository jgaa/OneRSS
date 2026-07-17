#include "browser_profile_manager.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <QUuid>

namespace onerss::desktop {
namespace {

struct BrowserVariant {
  QString suffix;
  QStringList arguments;
};

struct BrowserDefinition {
  QString display_name;
  QStringList executables;
  QVector<BrowserVariant> variants;
};

QString newProfileId() {
  return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString normalizedProgram(const QString &command) {
  return QFileInfo{command.trimmed()}.absoluteFilePath().toLower();
}

QString normalizedArguments(const QString &arguments) {
  const auto tokens = QProcess::splitCommand(arguments);
  return tokens.join(QChar{0x1f}).trimmed();
}

QVariantMap normalizeProfile(const QVariantMap &profile, bool allow_default) {
  QVariantMap normalized;
  normalized.insert(QStringLiteral("id"), profile.value(QStringLiteral("id")).toString().trimmed());
  normalized.insert(QStringLiteral("displayName"), profile.value(QStringLiteral("displayName")).toString().trimmed());
  normalized.insert(QStringLiteral("command"), profile.value(QStringLiteral("command")).toString().trimmed());
  normalized.insert(QStringLiteral("arguments"), profile.value(QStringLiteral("arguments")).toString().trimmed());
  normalized.insert(QStringLiteral("isDefault"), allow_default && profile.value(QStringLiteral("isDefault")).toBool());
  if (normalized.value(QStringLiteral("id")).toString().isEmpty()) {
    normalized.insert(QStringLiteral("id"), newProfileId());
  }
  return normalized;
}

QVariantList normalizeProfiles(const QVariantList &profiles) {
  QVariantList normalized;
  bool default_assigned = false;
  for (const auto &entry : profiles) {
    const auto profile = normalizeProfile(entry.toMap(), !default_assigned);
    if (profile.value(QStringLiteral("displayName")).toString().isEmpty()
        && profile.value(QStringLiteral("command")).toString().isEmpty()
        && profile.value(QStringLiteral("arguments")).toString().isEmpty()) {
      continue;
    }
    if (profile.value(QStringLiteral("isDefault")).toBool()) {
      default_assigned = true;
    }
    normalized.push_back(profile);
  }
  return normalized;
}

QVariantList loadProfiles() {
  QSettings settings;
  QVariantList profiles;
  const auto size = settings.beginReadArray(QStringLiteral("browserProfiles"));
  for (int index = 0; index < size; ++index) {
    settings.setArrayIndex(index);
    QVariantMap profile;
    profile.insert(QStringLiteral("id"), settings.value(QStringLiteral("id")).toString());
    profile.insert(QStringLiteral("displayName"), settings.value(QStringLiteral("displayName")).toString());
    profile.insert(QStringLiteral("command"), settings.value(QStringLiteral("command")).toString());
    profile.insert(QStringLiteral("arguments"), settings.value(QStringLiteral("arguments")).toString());
    profile.insert(QStringLiteral("isDefault"), settings.value(QStringLiteral("isDefault")).toBool());
    profiles.push_back(profile);
  }
  settings.endArray();
  return normalizeProfiles(profiles);
}

void saveProfiles(const QVariantList &profiles) {
  QSettings settings;
  settings.remove(QStringLiteral("browserProfiles"));
  settings.beginWriteArray(QStringLiteral("browserProfiles"));
  for (int index = 0; index < profiles.size(); ++index) {
    const auto profile = profiles.at(index).toMap();
    settings.setArrayIndex(index);
    settings.setValue(QStringLiteral("id"), profile.value(QStringLiteral("id")).toString());
    settings.setValue(QStringLiteral("displayName"), profile.value(QStringLiteral("displayName")).toString());
    settings.setValue(QStringLiteral("command"), profile.value(QStringLiteral("command")).toString());
    settings.setValue(QStringLiteral("arguments"), profile.value(QStringLiteral("arguments")).toString());
    settings.setValue(QStringLiteral("isDefault"), profile.value(QStringLiteral("isDefault")).toBool());
  }
  settings.endArray();
  settings.sync();
}

QStringList launchArguments(const QVariantMap &profile, const QString &encoded_url, QString *program) {
  QStringList tokens = QProcess::splitCommand(profile.value(QStringLiteral("command")).toString());
  QString executable;
  if (!tokens.isEmpty()) {
    executable = tokens.takeFirst();
  }
  tokens.append(QProcess::splitCommand(profile.value(QStringLiteral("arguments")).toString()));
  if (program != nullptr) {
    *program = executable;
  }
  bool placeholder_found = false;
  for (auto &token : tokens) {
    if (token == QStringLiteral("%u") || token == QStringLiteral("%U")) {
      token = encoded_url;
      placeholder_found = true;
    }
  }
  if (!placeholder_found) {
    tokens.push_back(encoded_url);
  }
  return tokens;
}

#ifdef Q_OS_LINUX
QVariantList detectedProfiles(const QVariantList &existing_profiles) {
  const QVector<BrowserDefinition> browsers{
    {
      .display_name = QStringLiteral("Firefox"),
      .executables = {QStringLiteral("firefox"), QStringLiteral("firefox-esr")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("-private-window")}},
      },
    },
    {
      .display_name = QStringLiteral("Brave"),
      .executables = {QStringLiteral("brave-browser"), QStringLiteral("brave")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--incognito")}},
      },
    },
    {
      .display_name = QStringLiteral("Tor Browser"),
      .executables = {QStringLiteral("tor-browser"), QStringLiteral("torbrowser-launcher")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("-private-window")}},
      },
    },
    {
      .display_name = QStringLiteral("Konqueror"),
      .executables = {QStringLiteral("konqueror")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--private")}},
      },
    },
    {
      .display_name = QStringLiteral("Chromium"),
      .executables = {QStringLiteral("chromium"), QStringLiteral("chromium-browser")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--incognito")}},
      },
    },
    {
      .display_name = QStringLiteral("Google Chrome"),
      .executables = {QStringLiteral("google-chrome-stable"), QStringLiteral("google-chrome")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--incognito")}},
      },
    },
    {
      .display_name = QStringLiteral("Microsoft Edge"),
      .executables = {QStringLiteral("microsoft-edge-stable"), QStringLiteral("microsoft-edge")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--inprivate")}},
      },
    },
    {
      .display_name = QStringLiteral("Vivaldi"),
      .executables = {QStringLiteral("vivaldi-stable"), QStringLiteral("vivaldi")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--incognito")}},
      },
    },
    {
      .display_name = QStringLiteral("Opera"),
      .executables = {QStringLiteral("opera")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--incognito")}},
      },
    },
    {
      .display_name = QStringLiteral("Falkon"),
      .executables = {QStringLiteral("falkon")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--private-browsing")}},
      },
    },
    {
      .display_name = QStringLiteral("qutebrowser"),
      .executables = {QStringLiteral("qutebrowser")},
      .variants = {
        {.suffix = QString{}, .arguments = {}},
        {.suffix = QStringLiteral("Private"), .arguments = {QStringLiteral("--target"), QStringLiteral("private-window")}},
        {.suffix = QStringLiteral("Private, No JavaScript"),
         .arguments = {
           QStringLiteral("--target"),
           QStringLiteral("private-window"),
           QStringLiteral("-s"),
           QStringLiteral("content.javascript.enabled=false"),
         }},
      },
    },
  };

  QVariantList merged = normalizeProfiles(existing_profiles);
  QSet<QString> fingerprints;
  for (const auto &entry : merged) {
    const auto profile = entry.toMap();
    fingerprints.insert(normalizedProgram(profile.value(QStringLiteral("command")).toString())
                        + QChar{0x1e}
                        + normalizedArguments(profile.value(QStringLiteral("arguments")).toString()));
  }

  for (const auto &browser : browsers) {
    QString executable;
    for (const auto &candidate : browser.executables) {
      executable = QStandardPaths::findExecutable(candidate);
      if (!executable.isEmpty()) {
        break;
      }
    }
    if (executable.isEmpty()) {
      continue;
    }

    for (const auto &variant : browser.variants) {
      const auto arguments = variant.arguments.join(QChar{' '});
      const auto fingerprint = normalizedProgram(executable) + QChar{0x1e} + normalizedArguments(arguments);
      if (fingerprints.contains(fingerprint)) {
        continue;
      }

      QVariantMap profile;
      profile.insert(QStringLiteral("id"), newProfileId());
      profile.insert(QStringLiteral("displayName"),
                     variant.suffix.isEmpty() ? browser.display_name
                                              : QStringLiteral("%1 (%2)").arg(browser.display_name, variant.suffix));
      profile.insert(QStringLiteral("command"), executable);
      profile.insert(QStringLiteral("arguments"), arguments);
      profile.insert(QStringLiteral("isDefault"), false);
      merged.push_back(profile);
      fingerprints.insert(fingerprint);
    }
  }

  return normalizeProfiles(merged);
}
#endif

}  // namespace

BrowserProfileManager::BrowserProfileManager(QObject *parent)
    : QObject(parent), browser_profiles_(loadProfiles()) {
}

QVariantList BrowserProfileManager::browserProfiles() const {
  return browser_profiles_;
}

void BrowserProfileManager::setBrowserProfiles(const QVariantList &profiles) {
  const auto normalized = normalizeProfiles(profiles);
  if (browser_profiles_ == normalized) {
    return;
  }
  browser_profiles_ = normalized;
  saveProfiles(browser_profiles_);
  emit browserProfilesChanged();
}

bool BrowserProfileManager::customBrowserProfilesSupported() const {
#ifdef Q_OS_LINUX
  return true;
#else
  return false;
#endif
}

void BrowserProfileManager::reload() {
  const auto loaded = loadProfiles();
  if (browser_profiles_ == loaded) {
    return;
  }
  browser_profiles_ = loaded;
  emit browserProfilesChanged();
}

QVariantList BrowserProfileManager::probeBrowserProfiles(const QVariantList &existing_profiles) const {
#ifdef Q_OS_LINUX
  return detectedProfiles(existing_profiles);
#else
  return normalizeProfiles(existing_profiles);
#endif
}

bool BrowserProfileManager::openUrl(const QString &url_text,
                                    const QString &profile_id,
                                    QString *error_message) const {
  const auto trimmed_url = url_text.trimmed();
  if (trimmed_url.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = tr("The article does not have a URL.");
    }
    return false;
  }

  const auto url = QUrl::fromUserInput(trimmed_url);
  if (!url.isValid()) {
    if (error_message != nullptr) {
      *error_message = tr("The article URL is not valid.");
    }
    return false;
  }

  QVariantMap selected_profile;
  if (!profile_id.trimmed().isEmpty()) {
    for (const auto &entry : browser_profiles_) {
      const auto profile = entry.toMap();
      if (profile.value(QStringLiteral("id")).toString() == profile_id) {
        selected_profile = profile;
        break;
      }
    }
    if (selected_profile.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = tr("The selected browser profile no longer exists.");
      }
      return false;
    }
  } else {
    for (const auto &entry : browser_profiles_) {
      const auto profile = entry.toMap();
      if (profile.value(QStringLiteral("isDefault")).toBool()) {
        selected_profile = profile;
        break;
      }
    }
  }

  if (selected_profile.isEmpty()) {
    const auto opened = QDesktopServices::openUrl(url);
    if (!opened && error_message != nullptr) {
      *error_message = tr("The system default browser could not be started.");
    }
    return opened;
  }

  QString program = selected_profile.value(QStringLiteral("command")).toString().trimmed();
  const auto arguments = launchArguments(selected_profile, url.toString(QUrl::FullyEncoded), &program);
  if (program.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = tr("The browser profile does not specify a command.");
    }
    return false;
  }

  const auto launched = QProcess::startDetached(program, arguments);
  if (!launched && error_message != nullptr) {
    *error_message = tr("Could not start %1.").arg(program);
  }
  return launched;
}

}  // namespace onerss::desktop
