#include "feed_fetcher.h"

#include "html_sanitizer.h"
#include "logging.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <future>
#include <sstream>
#include <stdexcept>

namespace onerss::backend {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = asio::ssl;
using tcp = asio::ip::tcp;

struct ParsedUrl {
  std::string host;
  std::string port;
  std::string target;
  bool https = false;
  std::string original;
};

ParsedUrl parseUrl(const std::string &url_text) {
  const auto scheme_pos = url_text.find("://");
  if (scheme_pos == std::string::npos) {
    throw std::runtime_error{"invalid feed URL"};
  }
  ParsedUrl result;
  const auto scheme = url_text.substr(0, scheme_pos);
  result.https = scheme == "https";
  if (scheme != "http" && scheme != "https") {
    throw std::runtime_error{"only http and https feeds are supported"};
  }

  const auto authority_start = scheme_pos + 3;
  const auto path_pos = url_text.find('/', authority_start);
  const auto authority = url_text.substr(authority_start, path_pos == std::string::npos
                                                           ? std::string::npos
                                                           : path_pos - authority_start);
  const auto colon_pos = authority.rfind(':');
  if (colon_pos != std::string::npos) {
    result.host = authority.substr(0, colon_pos);
    result.port = authority.substr(colon_pos + 1);
  } else {
    result.host = authority;
    result.port = result.https ? "443" : "80";
  }
  result.target = path_pos == std::string::npos ? "/" : url_text.substr(path_pos);
  if (result.host.empty()) {
    throw std::runtime_error{"feed URL host is empty"};
  }
  if (result.target.empty()) {
    result.target = "/";
  }
  result.original = url_text;
  return result;
}

struct FetchResult {
  unsigned status = 0;
  std::string body;
  std::string location;
};

std::string resolveRedirectUrl(const ParsedUrl &base, const std::string &location) {
  if (location.starts_with("http://") || location.starts_with("https://")) {
    return location;
  }

  const auto scheme = base.https ? "https" : "http";
  if (location.starts_with("//")) {
    return std::string{scheme} + ":" + location;
  }

  if (!location.empty() && location.front() == '/') {
    return std::string{scheme} + "://" + base.host + ":" + base.port + location;
  }

  const auto slash = base.target.rfind('/');
  const auto prefix = slash == std::string::npos ? "/" : base.target.substr(0, slash + 1);
  return std::string{scheme} + "://" + base.host + ":" + base.port + prefix + location;
}

asio::awaitable<FetchResult> fetchOnce(const ParsedUrl &url) {
  auto executor = co_await asio::this_coro::executor;
  tcp::resolver resolver{executor};
  const auto endpoints = co_await resolver.async_resolve(url.host, url.port, asio::use_awaitable);

  http::request<http::empty_body> request{http::verb::get, url.target, 11};
  request.set(http::field::host, url.host);
  request.set(http::field::user_agent, "OneRSS/0.1");
  request.set(http::field::accept, "application/rss+xml, application/xml, text/xml, */*");

  beast::flat_buffer buffer;
  http::response<http::string_body> response;

  if (url.https) {
    ssl::context ssl_context{ssl::context::tls_client};
    ssl_context.set_default_verify_paths();
    beast::ssl_stream<beast::tcp_stream> stream{executor, ssl_context};
    SSL_set_tlsext_host_name(stream.native_handle(), url.host.c_str());
    co_await beast::get_lowest_layer(stream).async_connect(endpoints, asio::use_awaitable);
    co_await stream.async_handshake(ssl::stream_base::client, asio::use_awaitable);
    co_await http::async_write(stream, request, asio::use_awaitable);
    co_await http::async_read(stream, buffer, response, asio::use_awaitable);
    beast::error_code ignored;
    co_await stream.async_shutdown(asio::redirect_error(asio::use_awaitable, ignored));
  } else {
    beast::tcp_stream stream{executor};
    co_await stream.async_connect(endpoints, asio::use_awaitable);
    co_await http::async_write(stream, request, asio::use_awaitable);
    co_await http::async_read(stream, buffer, response, asio::use_awaitable);
    beast::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
  }

  FetchResult result;
  result.status = static_cast<unsigned>(response.result_int());
  result.body = response.body();
  if (const auto it = response.find(http::field::location); it != response.end()) {
    result.location = std::string{it->value()};
  }
  co_return result;
}

asio::awaitable<std::string> fetchBody(const ParsedUrl &initial_url) {
  ParsedUrl current = initial_url;
  for (int redirects = 0; redirects < 5; ++redirects) {
    auto response = co_await fetchOnce(current);
    if (response.status >= 200 && response.status < 300) {
      co_return response.body;
    }

    if (response.status == 301 || response.status == 302 || response.status == 303
        || response.status == 307 || response.status == 308) {
      if (response.location.empty()) {
        throw std::runtime_error{"redirect response without location header"};
      }
      const auto next_url = resolveRedirectUrl(current, response.location);
      LOG_INFO << "Following redirect from " << current.original << " to " << next_url;
      current = parseUrl(next_url);
      continue;
    }

    throw std::runtime_error{"feed fetch failed with HTTP status " + std::to_string(response.status)};
  }

  throw std::runtime_error{"too many redirects while fetching feed"};
}

std::string childValue(const boost::property_tree::ptree &node, const char *path) {
  return node.get<std::string>(path, "");
}

std::string firstNonEmpty(std::initializer_list<std::string> values) {
  for (const auto &value : values) {
    if (!value.empty()) {
      return value;
    }
  }
  return {};
}

std::vector<ArticleRecord> parseRss(const boost::property_tree::ptree &channel, const TreeNodeRecord &feed_node) {
  std::vector<ArticleRecord> articles;
  for (const auto &entry : channel) {
    if (entry.first != "item") {
      continue;
    }

    const auto &item = entry.second;
    const auto title = childValue(item, "title");
    const auto link = childValue(item, "link");
    const auto guid = firstNonEmpty({childValue(item, "guid"), link, title});
    if (guid.empty()) {
      continue;
    }

    articles.push_back(ArticleRecord{
      .article_id = {},
      .user_id = {},
      .node_id = feed_node.node_id,
      .guid = guid,
      .title = title,
      .link_url = link,
      .published_at = firstNonEmpty({childValue(item, "pubDate"), childValue(item, "dc:date")}),
      .author = firstNonEmpty({childValue(item, "author"), childValue(item, "dc:creator")}),
      .content = sanitizePreviewHtml(firstNonEmpty({childValue(item, "content:encoded"), childValue(item, "description")})),
    });
  }
  return articles;
}

std::vector<ArticleRecord> parseAtom(const boost::property_tree::ptree &feed, const TreeNodeRecord &feed_node) {
  std::vector<ArticleRecord> articles;
  for (const auto &entry : feed) {
    if (entry.first != "entry") {
      continue;
    }

    const auto &item = entry.second;
    std::string link;
    for (const auto &candidate : item) {
      if (candidate.first == "link") {
        link = candidate.second.get<std::string>("<xmlattr>.href", "");
        if (!link.empty()) {
          break;
        }
      }
    }

    const auto title = childValue(item, "title");
    const auto guid = firstNonEmpty({childValue(item, "id"), link, title});
    if (guid.empty()) {
      continue;
    }

    articles.push_back(ArticleRecord{
      .article_id = {},
      .user_id = {},
      .node_id = feed_node.node_id,
      .guid = guid,
      .title = title,
      .link_url = link,
      .published_at = firstNonEmpty({childValue(item, "updated"), childValue(item, "published")}),
      .author = childValue(item, "author.name"),
      .content = sanitizePreviewHtml(firstNonEmpty({childValue(item, "content"), childValue(item, "summary")})),
    });
  }
  return articles;
}

std::vector<ArticleRecord> parseFeedXml(const std::string &body, const TreeNodeRecord &feed_node) {
  std::istringstream input{body};
  boost::property_tree::ptree document;
  boost::property_tree::read_xml(input, document, boost::property_tree::xml_parser::trim_whitespace);

  if (const auto rss = document.get_child_optional("rss.channel")) {
    return parseRss(*rss, feed_node);
  }
  if (const auto atom = document.get_child_optional("feed")) {
    return parseAtom(*atom, feed_node);
  }
  throw std::runtime_error{"unsupported feed format"};
}

}  // namespace

std::vector<ArticleRecord> FeedFetcher::fetchArticles(const TreeNodeRecord &feed_node) const {
  LOG_INFO << "Refreshing feed node=" << feed_node.node_id << " url=" << feed_node.feed_url;
  asio::io_context io_context;
  std::promise<std::string> promise;
  auto future = promise.get_future();

  const auto url = parseUrl(feed_node.feed_url);
  asio::co_spawn(
    io_context,
    [url, &promise]() -> asio::awaitable<void> {
      try {
        promise.set_value(co_await fetchBody(url));
      } catch (...) {
        promise.set_exception(std::current_exception());
      }
      co_return;
    },
    asio::detached);

  io_context.run();
  const auto body = future.get();
  auto articles = parseFeedXml(body, feed_node);
  LOG_INFO << "Fetched " << articles.size() << " articles for feed node=" << feed_node.node_id;
  return articles;
}

}  // namespace onerss::backend
