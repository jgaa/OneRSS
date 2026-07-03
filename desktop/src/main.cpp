#include "app_info.h"
#include "logging.h"
#include "main_view_model.h"
#include "signup_view_model.h"
#include "tray_controller.h"

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWindow>

int main(int argc, char *argv[]) {
  QApplication::setOrganizationName(QStringLiteral("JGAA"));
  QApplication::setOrganizationDomain(QStringLiteral("jgaa.example"));
  QApplication::setApplicationName(QStringLiteral("OneRSS"));

  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);

  onerss::desktop::LoggingController logging;
  logging.initialize();
  LOG_INFO << "Starting OneRSS desktop app";
  LOG_DEBUG << "QSettings file path: " << logging.settingsFilePath().toStdString();

  onerss::desktop::AppInfo app_info;
  onerss::desktop::MainViewModel main_view_model;
  onerss::desktop::SignupViewModel signup_view_model;
  onerss::desktop::TrayController tray_controller;
  QObject::connect(&signup_view_model,
                   &onerss::desktop::SignupViewModel::peerMaterialStored,
                   &main_view_model,
                   &onerss::desktop::MainViewModel::onPeerMaterialStored);
  QObject::connect(&main_view_model,
                   &onerss::desktop::MainViewModel::unreadCountChanged,
                   &tray_controller,
                   [&main_view_model, &tray_controller]() { tray_controller.setUnreadCount(main_view_model.unreadCount()); });

  QQmlApplicationEngine engine;
  engine.rootContext()->setContextProperty(QStringLiteral("AppInfo"), &app_info);
  engine.rootContext()->setContextProperty(QStringLiteral("mainViewModel"), &main_view_model);
  engine.rootContext()->setContextProperty(QStringLiteral("signupViewModel"), &signup_view_model);
  engine.rootContext()->setContextProperty(QStringLiteral("trayController"), &tray_controller);
  engine.loadFromModule("OneRSS", "Main");

  if (engine.rootObjects().isEmpty()) {
    return 1;
  }

  if (auto *window = qobject_cast<QWindow *>(engine.rootObjects().constFirst())) {
    tray_controller.initialize(window);
    tray_controller.setUnreadCount(main_view_model.unreadCount());
  }

  return app.exec();
}
