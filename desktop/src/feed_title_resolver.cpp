#include "feed_title_resolver.h"

#include "logging.h"

#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

namespace onerss::desktop {

namespace {

QString parseFeedTitle(const QByteArray &document) {
  QXmlStreamReader xml{document};
  QStringList path;

  while (!xml.atEnd()) {
    xml.readNext();
    if (xml.isStartElement()) {
      path.push_back(xml.name().toString().toLower());
      const auto matches_rss_title = path.size() >= 3 && path[path.size() - 3] == QStringLiteral("rss")
                                     && path[path.size() - 2] == QStringLiteral("channel")
                                     && path[path.size() - 1] == QStringLiteral("title");
      const auto matches_atom_title
        = path.size() >= 2 && path[path.size() - 2] == QStringLiteral("feed") && path[path.size() - 1] == QStringLiteral("title");
      if (matches_rss_title || matches_atom_title) {
        return xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
      }
    } else if (xml.isEndElement() && !path.isEmpty()) {
      path.removeLast();
    }
  }

  if (xml.hasError()) {
    LOG_DEBUG << "Failed to parse feed title from XML: " << xml.errorString().toStdString();
  }
  return {};
}

}  // namespace

std::optional<QString> tryResolveFeedTitle(const QString &feed_url) {
  const auto trimmed_url = feed_url.trimmed();
  if (trimmed_url.isEmpty()) {
    return std::nullopt;
  }

  const auto url = QUrl::fromUserInput(trimmed_url);
  if (!url.isValid() || url.scheme().isEmpty()) {
    LOG_DEBUG << "Skipping feed title lookup because URL is invalid: " << trimmed_url.toStdString();
    return std::nullopt;
  }

  LOG_DEBUG << "Attempting local feed title lookup for url=" << trimmed_url.toStdString();

  QNetworkAccessManager network;
  QNetworkRequest request{url};
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  request.setRawHeader("Accept", "application/atom+xml, application/rss+xml, application/xml, text/xml, */*");
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OneRSS Desktop"));

  QEventLoop loop;
  QTimer timeout;
  timeout.setSingleShot(true);

  auto *reply = network.get(request);
  QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
  QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
  QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
  timeout.start(4000);
  loop.exec();
  timeout.stop();

  const auto error = reply->error();
  const auto error_string = reply->errorString();
  const auto body = reply->readAll();
  reply->deleteLater();

  if (error != QNetworkReply::NoError) {
    LOG_DEBUG << "Feed title lookup failed for url=" << trimmed_url.toStdString() << ": " << error_string.toStdString();
    return std::nullopt;
  }

  const auto title = parseFeedTitle(body);
  if (title.isEmpty()) {
    LOG_DEBUG << "Feed title lookup did not find a title for url=" << trimmed_url.toStdString();
    return std::nullopt;
  }

  LOG_INFO << "Resolved local feed title for url=" << trimmed_url.toStdString() << " title=" << title.toStdString();
  return title;
}

}  // namespace onerss::desktop
