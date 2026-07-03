#pragma once

#include <QObject>

class QMenu;
class QSystemTrayIcon;
class QWindow;

namespace onerss::desktop {

class TrayController final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool trayAvailable READ trayAvailable CONSTANT)

 public:
  explicit TrayController(QObject *parent = nullptr);
  ~TrayController() override;

  [[nodiscard]] bool trayAvailable() const;
  void initialize(QWindow *window);

 public slots:
  void setUnreadCount(int unread_count);
  void showMainWindow();
  void hideMainWindow();
  void toggleMainWindow();
  void quitApplication();

 signals:
  void requestQuit();

 private:
  void updateIcon();
  void updateToolTip();

  QWindow *window_ = nullptr;
  QSystemTrayIcon *tray_icon_ = nullptr;
  QMenu *menu_ = nullptr;
  int unread_count_ = 0;
};

}  // namespace onerss::desktop
