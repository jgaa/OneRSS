#pragma once

#include "secret_store.h"
#include "signup_client.h"

#include <QObject>
#include <QString>

namespace onerss::desktop {

class SignupViewModel final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString serverHost READ serverHost WRITE setServerHost NOTIFY serverHostChanged)
  Q_PROPERTY(int serverPort READ serverPort WRITE setServerPort NOTIFY serverPortChanged)
  Q_PROPERTY(QString login READ login WRITE setLogin NOTIFY loginChanged)
  Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY passwordChanged)
  Q_PROPERTY(QString deviceName READ deviceName WRITE setDeviceName NOTIFY deviceNameChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

 public:
  explicit SignupViewModel(QObject *parent = nullptr);
  ~SignupViewModel() override;

  [[nodiscard]] QString serverHost() const;
  [[nodiscard]] int serverPort() const;
  [[nodiscard]] QString login() const;
  [[nodiscard]] QString password() const;
  [[nodiscard]] QString deviceName() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool busy() const;

  void setServerHost(const QString &value);
  void setServerPort(int value);
  void setLogin(const QString &value);
  void setPassword(const QString &value);
  void setDeviceName(const QString &value);

  Q_INVOKABLE void createAccount();
  Q_INVOKABLE void pairDevice();

 signals:
  void peerMaterialStored();
  void serverHostChanged();
  void serverPortChanged();
  void loginChanged();
  void passwordChanged();
  void deviceNameChanged();
  void statusChanged();
  void busyChanged();

 private:
  void startRequest(bool create_account);
  void setBusy(bool value);
  void setStatus(const QString &value);

  QString server_host_ = QStringLiteral("127.0.0.1");
  int server_port_ = 7443;
  QString login_;
  QString password_;
  QString device_name_;
  QString status_ = QStringLiteral("Ready");
  bool busy_ = false;
  SignupClient signup_client_;
  SecretStore secret_store_;
};

}  // namespace onerss::desktop
