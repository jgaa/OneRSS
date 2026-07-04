#pragma once

#include <QObject>
#include <QString>

namespace onerss::desktop {

class AppInfo final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString description READ description CONSTANT)
  Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
  Q_PROPERTY(QString qtVersion READ qtVersion CONSTANT)
  Q_PROPERTY(QString tlsLibraryVersion READ tlsLibraryVersion CONSTANT)
  Q_PROPERTY(QString protobufVersion READ protobufVersion CONSTANT)
  Q_PROPERTY(QString compiler READ compiler CONSTANT)
  Q_PROPERTY(QString buildDate READ buildDate CONSTANT)

 public:
  explicit AppInfo(QObject *parent = nullptr);

  [[nodiscard]] QString description() const;
  [[nodiscard]] QString applicationVersion() const;
  [[nodiscard]] QString qtVersion() const;
  [[nodiscard]] QString tlsLibraryVersion() const;
  [[nodiscard]] QString protobufVersion() const;
  [[nodiscard]] QString compiler() const;
  [[nodiscard]] QString buildDate() const;
};

}  // namespace onerss::desktop
