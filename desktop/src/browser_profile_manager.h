#pragma once

#include <QObject>
#include <QVariantList>

namespace onerss::desktop {

class BrowserProfileManager final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantList browserProfiles READ browserProfiles WRITE setBrowserProfiles NOTIFY browserProfilesChanged)
  Q_PROPERTY(bool customBrowserProfilesSupported READ customBrowserProfilesSupported CONSTANT)

 public:
  explicit BrowserProfileManager(QObject *parent = nullptr);

  [[nodiscard]] QVariantList browserProfiles() const;
  void setBrowserProfiles(const QVariantList &profiles);
  [[nodiscard]] bool customBrowserProfilesSupported() const;

  Q_INVOKABLE void reload();
  Q_INVOKABLE QVariantList probeBrowserProfiles(const QVariantList &existing_profiles) const;
  [[nodiscard]] bool openUrl(const QString &url_text,
                             const QString &profile_id = QString{},
                             QString *error_message = nullptr) const;

 signals:
  void browserProfilesChanged();

 private:
  QVariantList browser_profiles_;
};

}  // namespace onerss::desktop
