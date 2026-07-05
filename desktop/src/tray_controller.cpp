#include "tray_controller.h"

#include "logging.h"

#include <QApplication>
#include <QAction>
#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSystemTrayIcon>
#include <QWindow>

namespace onerss::desktop {
namespace {

QIcon baseIcon() {
  const QIcon bundled_icon{QStringLiteral(":/icons/onerss.svg")};
  if (!bundled_icon.isNull()) {
    return bundled_icon;
  }

  auto icon = QIcon::fromTheme(QStringLiteral("application-rss+xml"));
  if (!icon.isNull()) {
    return icon;
  }

  QPixmap pixmap{32, 32};
  pixmap.fill(Qt::transparent);
  QPainter painter{&pixmap};
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setBrush(QColor{"#f57c00"});
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(QRectF{0, 0, 32, 32}, 6, 6);
  painter.setBrush(Qt::white);
  painter.drawEllipse(QPointF{8, 24}, 3, 3);
  QPen pen{Qt::white, 3.0, Qt::SolidLine, Qt::RoundCap};
  painter.setPen(pen);
  painter.drawArc(QRectF{4, 12, 16, 16}, 0, 90 * 16);
  painter.drawArc(QRectF{4, 4, 24, 24}, 0, 90 * 16);
  return QIcon{pixmap};
}

QString badgeText(const int unread_count) {
  if (unread_count <= 0) {
    return {};
  }
  return unread_count > 99 ? QStringLiteral("99+") : QString::number(unread_count);
}

QIcon badgedIcon(const int unread_count) {
  const auto icon = baseIcon();
  if (unread_count <= 0) {
    return icon;
  }

  QPixmap pixmap = icon.pixmap(32, 32);
  QPainter painter{&pixmap};
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QRect badge_rect{12, 0, 20, 14};
  painter.setBrush(QColor{"#c62828"});
  painter.setPen(Qt::NoPen);
  painter.drawRoundedRect(badge_rect, 7, 7);
  painter.setPen(Qt::white);
  QFont font = QGuiApplication::font();
  font.setBold(true);
  font.setPixelSize(9);
  painter.setFont(font);
  painter.drawText(badge_rect, Qt::AlignCenter, badgeText(unread_count));
  return QIcon{pixmap};
}

}  // namespace

TrayController::TrayController(QObject *parent) : QObject(parent) {}

TrayController::~TrayController() = default;

bool TrayController::trayAvailable() const {
  return QSystemTrayIcon::isSystemTrayAvailable();
}

void TrayController::initialize(QWindow *window) {
  window_ = window;
  if (!trayAvailable()) {
    LOG_INFO << "System tray is not available";
    return;
  }

  tray_icon_ = new QSystemTrayIcon(this);
  menu_ = new QMenu();
  auto *show_action = menu_->addAction(tr("Show OneRSS"));
  auto *hide_action = menu_->addAction(tr("Hide OneRSS"));
  menu_->addSeparator();
  auto *quit_action = menu_->addAction(tr("Quit"));
  connect(show_action, &QAction::triggered, this, &TrayController::showMainWindow);
  connect(hide_action, &QAction::triggered, this, &TrayController::hideMainWindow);
  connect(quit_action, &QAction::triggered, this, &TrayController::quitApplication);
  connect(tray_icon_, &QSystemTrayIcon::activated, this, [this](const QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
      toggleMainWindow();
    }
  });
  tray_icon_->setContextMenu(menu_);
  updateIcon();
  updateToolTip();
  tray_icon_->show();
  LOG_INFO << "System tray icon initialized";
}

void TrayController::setUnreadCount(const int unread_count) {
  if (unread_count_ == unread_count) {
    return;
  }
  unread_count_ = std::max(0, unread_count);
  updateIcon();
  updateToolTip();
}

void TrayController::showMainWindow() {
  if (window_ == nullptr) {
    return;
  }
  window_->show();
  window_->raise();
  window_->requestActivate();
}

void TrayController::hideMainWindow() {
  if (window_ == nullptr) {
    return;
  }
  window_->hide();
}

void TrayController::toggleMainWindow() {
  if (window_ == nullptr) {
    return;
  }
  if (window_->isVisible()) {
    hideMainWindow();
  } else {
    showMainWindow();
  }
}

void TrayController::quitApplication() {
  emit requestQuit();
  QApplication::quit();
}

void TrayController::updateIcon() {
  if (tray_icon_ == nullptr) {
    return;
  }
  tray_icon_->setIcon(badgedIcon(unread_count_));
}

void TrayController::updateToolTip() {
  if (tray_icon_ == nullptr) {
    return;
  }
  tray_icon_->setToolTip(unread_count_ > 0 ? tr("OneRSS (%1 unread)").arg(badgeText(unread_count_))
                                           : tr("OneRSS"));
}

}  // namespace onerss::desktop
