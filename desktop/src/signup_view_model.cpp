#include "signup_view_model.h"

#include "logging.h"

#include <QMetaObject>
#include <QSysInfo>

#include <thread>
#include <utility>

namespace onerss::desktop {

SignupViewModel::SignupViewModel(QObject *parent) : QObject{parent} {
  device_name_ = QSysInfo::machineHostName();
  try {
    secret_store_.initialize();
  } catch (const std::exception &error) {
    LOG_WARN << "SafeKeeping initialization is not available yet: " << error.what();
    status_ = QStringLiteral("Secret storage unavailable until a desktop vault is reachable");
  }
}

SignupViewModel::~SignupViewModel() = default;

QString SignupViewModel::serverHost() const {
  return server_host_;
}

int SignupViewModel::serverPort() const {
  return server_port_;
}

QString SignupViewModel::login() const {
  return login_;
}

QString SignupViewModel::password() const {
  return password_;
}

QString SignupViewModel::deviceName() const {
  return device_name_;
}

QString SignupViewModel::status() const {
  return status_;
}

bool SignupViewModel::busy() const {
  return busy_;
}

void SignupViewModel::setServerHost(const QString &value) {
  if (server_host_ == value) {
    return;
  }
  server_host_ = value;
  emit serverHostChanged();
}

void SignupViewModel::setServerPort(const int value) {
  if (server_port_ == value) {
    return;
  }
  server_port_ = value;
  emit serverPortChanged();
}

void SignupViewModel::setLogin(const QString &value) {
  if (login_ == value) {
    return;
  }
  login_ = value;
  emit loginChanged();
}

void SignupViewModel::setPassword(const QString &value) {
  if (password_ == value) {
    return;
  }
  password_ = value;
  emit passwordChanged();
}

void SignupViewModel::setDeviceName(const QString &value) {
  if (device_name_ == value) {
    return;
  }
  device_name_ = value;
  emit deviceNameChanged();
}

void SignupViewModel::createAccount() {
  startRequest(true);
}

void SignupViewModel::pairDevice() {
  startRequest(false);
}

void SignupViewModel::startRequest(const bool create_account) {
  if (busy_) {
    LOG_DEBUG << "Ignoring signup request while another request is active";
    return;
  }

  const auto host = server_host_;
  const auto port = server_port_;
  const auto login = login_;
  const auto password = password_;
  const auto device_name = device_name_;

  setBusy(true);
  setStatus(create_account ? QStringLiteral("Creating account...") : QStringLiteral("Pairing device..."));

  std::thread([this,
               create_account,
               host = host.toStdString(),
               port,
               login = login.toStdString(),
               password = password.toStdString(),
               device_name = device_name.toStdString()]() {
    try {
      LOG_DEBUG << "Desktop signup worker started for login=" << login;
      const auto response = signup_client_.signupOrPair(
        host, static_cast<std::uint16_t>(port), create_account, login, password, device_name);
      secret_store_.storeSignupMaterial(response, QString::fromStdString(host), port);
      const auto status = QStringLiteral("Success: %1 (%2)")
                            .arg(QString::fromStdString(response.user_id()),
                                 QString::fromStdString(response.device_id()));
      QMetaObject::invokeMethod(this,
                                [this, status]() {
                                  setBusy(false);
                                  setStatus(status);
                                  emit peerMaterialStored();
                                },
                                Qt::QueuedConnection);
    } catch (const std::exception &error) {
      LOG_WARN << "Desktop signup failed: " << error.what();
      const auto status = QStringLiteral("Failed: %1").arg(QString::fromUtf8(error.what()));
      QMetaObject::invokeMethod(this,
                                [this, status]() {
                                  setBusy(false);
                                  setStatus(status);
                                },
                                Qt::QueuedConnection);
    }
  }).detach();
}

void SignupViewModel::setBusy(const bool value) {
  if (busy_ == value) {
    return;
  }
  busy_ = value;
  emit busyChanged();
}

void SignupViewModel::setStatus(const QString &value) {
  if (status_ == value) {
    return;
  }

  if (value.startsWith(QStringLiteral("Failed:"))) {
    LOG_ERROR << "Signup UI status updated with failure: " << value.toStdString();
  } else {
    LOG_DEBUG << "Signup UI status updated: " << value.toStdString();
  }

  status_ = value;
  emit statusChanged();
}

}  // namespace onerss::desktop
