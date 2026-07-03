#include "app_info.h"

#include <boost/config.hpp>
#include <boost/version.hpp>
#include <google/protobuf/stubs/common.h>
#include <openssl/opensslv.h>
#include <sqlite3.h>

namespace onerss::desktop {
namespace {

QString boostVersionString() {
  const auto major = BOOST_VERSION / 100000;
  const auto minor = BOOST_VERSION / 100 % 1000;
  const auto patch = BOOST_VERSION % 100;
  return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

QString protobufVersionString() {
  const auto version = GOOGLE_PROTOBUF_VERSION;
  const auto major = version / 1000000;
  const auto minor = version / 1000 % 1000;
  const auto patch = version % 1000;
  return QStringLiteral("%1.%2.%3").arg(major).arg(minor).arg(patch);
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

QString AppInfo::boostVersion() const {
  return boostVersionString();
}

QString AppInfo::opensslVersion() const {
  return QString::fromLatin1(OPENSSL_VERSION_TEXT);
}

QString AppInfo::protobufVersion() const {
  return protobufVersionString();
}

QString AppInfo::sqliteVersion() const {
  return QString::fromLatin1(sqlite3_libversion());
}

QString AppInfo::compiler() const {
  return QString::fromLatin1(BOOST_COMPILER);
}

QString AppInfo::buildDate() const {
  return QStringLiteral(__DATE__ " " __TIME__);
}

}  // namespace onerss::desktop
