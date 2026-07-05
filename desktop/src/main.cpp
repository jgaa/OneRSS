#include "app_info.h"
#include "logging.h"
#include "main_view_model.h"
#include "signup_view_model.h"

#ifndef __ANDROID__
#   include "tray_controller.h"
#   include <QApplication>
#else
#   include <QGuiApplication>
#endif
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QQuickWindow>
#include <QWindow>

int main(int argc, char *argv[]) {

#ifdef __ANDROID__
  QGuiApplication::setOrganizationName(QStringLiteral("JGAA"));
  QGuiApplication::setOrganizationDomain(QStringLiteral("jgaa.example"));
  QGuiApplication::setApplicationName(QStringLiteral("OneRSS"));
  QGuiApplication app(argc, argv);
#else
  QApplication::setOrganizationName(QStringLiteral("JGAA"));
  QApplication::setOrganizationDomain(QStringLiteral("jgaa.example"));
  QApplication::setApplicationName(QStringLiteral("OneRSS"));
  QApplication app(argc, argv);
#endif

  app.setQuitOnLastWindowClosed(false);
  const QIcon app_icon{QStringLiteral(":/icons/onerss.svg")};
  if (!app_icon.isNull()) {
    app.setWindowIcon(app_icon);
  }

  onerss::desktop::LoggingController logging;
  logging.initialize();
  LOG_INFO << "Starting OneRSS " << ONERSS_VERSION << " desktop app";
  LOG_DEBUG << "QSettings file path: " << logging.settingsFilePath().toStdString();
  LOG_DEBUG << "SSL support " << (QSslSocket::supportsSsl() ? "enabled" : "disabled");

  onerss::desktop::AppInfo app_info;
  onerss::desktop::MainViewModel main_view_model;
  onerss::desktop::SignupViewModel signup_view_model;

  QObject::connect(&signup_view_model,
                   &onerss::desktop::SignupViewModel::peerMaterialStored,
                   &main_view_model,
                   &onerss::desktop::MainViewModel::onPeerMaterialStored);

#ifndef __ANDROID__
  onerss::desktop::TrayController tray_controller;
  QObject::connect(&main_view_model,
                   &onerss::desktop::MainViewModel::unreadCountChanged,
                   &tray_controller,
                   [&main_view_model, &tray_controller]() { tray_controller.setUnreadCount(main_view_model.unreadCount()); });
#endif

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("AppInfo"), &app_info);
  engine.rootContext()->setContextProperty(QStringLiteral("mainViewModel"), &main_view_model);
  engine.rootContext()->setContextProperty(QStringLiteral("signupViewModel"), &signup_view_model);

#ifndef __ANDROID__
  engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &tray_controller);
#endif

  engine.loadFromModule("OneRSS", "Main");

  if (engine.rootObjects().isEmpty()) {
    return 1;
  }

#ifndef __ANDROID__
  if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
    if (!app_icon.isNull()) {
      window->setIcon(app_icon);
    }
    tray_controller.initialize(window);
    tray_controller.setUnreadCount(main_view_model.unreadCount());
  }
#endif

  return app.exec();
}
