#include "app_info.h"

#include <QtNetwork/QSslSocket>
#include <QtProtobuf/qtprotobufversion.h>

namespace onerss::desktop {
namespace {

QString protobufVersionString() {
  const auto version = QTPROTOBUF_VERSION;
  const auto major = version >> 16;
  const auto minor = (version >> 8) & 0xff;
  const auto patch = version & 0xff;
  return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString compilerString() {
#if defined(__clang__)
  return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
  return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#elif defined(_MSC_VER)
  return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
  return QStringLiteral("Unknown");
#endif
}

}  // namespace

AppInfo::AppInfo(QObject *parent) : QObject(parent) {}

QString AppInfo::description() const {
  return tr(R"(OneRSS is a native RSS reader with a synchronized backend.

The desktop app is intended to feel familiar to Akregator users: a feed tree on the left, an article list on the top right, and an article preview on the bottom right.

Security stays ahead of convenience. Article rendering is intentionally constrained, the sync protocol is small, and secrets stay in protected storage rather than loose files.)");
}

QString AppInfo::applicationVersion() const {
  return QStringLiteral(ONERSS_VERSION);
}

QString AppInfo::qtVersion() const {
  return QString::fromLatin1(qVersion());
}

QString AppInfo::tlsLibraryVersion() const {
  const auto version = QSslSocket::sslLibraryVersionString();
  return version.isEmpty() ? tr("Unavailable") : version;
}

QString AppInfo::protobufVersion() const {
  return protobufVersionString();
}

QString AppInfo::compiler() const {
  return compilerString();
}

QString AppInfo::buildDate() const {
  return QStringLiteral(__DATE__ " " __TIME__);
}

}  // namespace onerss::desktop
