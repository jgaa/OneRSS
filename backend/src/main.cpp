#include "feed_fetcher.h"
#include "logging.h"
#include "app_server.h"
#include "sqlite_tree_repository.h"
#include "signup_server.h"
#include "ssl_util.h"
#include "user_store.h"

#include <boost/program_options.hpp>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

std::optional<logfault::LogLevel> toConsoleLevel(const std::string &name) {
  const auto level = onerss::backend::logging::parseLogLevel(name, logfault::LogLevel::INFO);
  if (level == logfault::LogLevel::DISABLED) {
    return std::nullopt;
  }
  return level;
}

}  // namespace

int main(int argc, char **argv) {
  namespace po = boost::program_options;

  try {
    std::filesystem::path database_path = "onerss.sqlite3";
    std::string bind_address = "0.0.0.0";
    unsigned int signup_port = 7443;
    unsigned int app_port = 7444;
    unsigned int num_concurrent_fectces = 6;
#ifndef NDEBUG
    std::string log_file = "/tmp/onerss-backend.log";
    std::string log_level = "trace";
#else
    std::string log_file = "/tmp/onerss-backend.log";
    std::string log_level = "info";
#endif
    std::string console_level = "info";

    po::options_description options{"OneRSS backend options"};
    options.add_options()
      ("help,h", "Show help")
      ("db", po::value<std::filesystem::path>(&database_path)->default_value(database_path),
       "SQLite database path")
      ("bind", po::value<std::string>(&bind_address)->default_value(bind_address), "Bind address")
      ("signup-port", po::value<unsigned int>(&signup_port)->default_value(signup_port),
       "Signup listen port")
      ("app-port", po::value<unsigned int>(&app_port)->default_value(app_port), "Authenticated app listen port")
      ("num-concurrent-fectces",
       po::value<unsigned int>(&num_concurrent_fectces)->default_value(num_concurrent_fectces),
       "Maximum number of concurrent scheduled feed fetches")
      ("log-file", po::value<std::string>(&log_file)->default_value(log_file), "Path to the log file")
      ("log-level", po::value<std::string>(&log_level)->default_value(log_level),
       "File log level: off, error, warn, notice, info, debug, trace")
      ("console-log-level", po::value<std::string>(&console_level)->default_value(console_level),
       "Console log level: off, error, warn, notice, info, debug, trace");

    po::variables_map values;
    po::store(po::parse_command_line(argc, argv, options), values);
    po::notify(values);

    if (values.contains("help")) {
      std::cout << options << '\n';
      return 0;
    }

    onerss::backend::initializeOpenSsl();
    onerss::backend::logging::configure(
      log_file,
      toConsoleLevel(console_level),
      onerss::backend::logging::parseLogLevel(log_level, logfault::LogLevel::INFO),
      true);

    LOG_INFO << "Starting OneRSS backend";
    LOG_DEBUG << "Database path: " << database_path;
    LOG_DEBUG << "Bind address signup=" << bind_address << ":" << signup_port
              << " app=" << bind_address << ":" << app_port
              << " scheduled_fetch_workers=" << num_concurrent_fectces;

    onerss::backend::UserStore user_store{database_path};
    onerss::backend::SqliteTreeRepository tree_repository{database_path};
    onerss::backend::FeedFetcher feed_fetcher;
    onerss::backend::SignupServer signup_server{
      user_store, bind_address, static_cast<std::uint16_t>(signup_port), static_cast<std::uint16_t>(app_port)};
    onerss::backend::AppServer app_server{
      user_store,
      tree_repository,
      feed_fetcher,
      num_concurrent_fectces,
      bind_address,
      static_cast<std::uint16_t>(app_port)};

    std::jthread signup_thread([&signup_server]() { signup_server.run(); });
    app_server.run();
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return 1;
  }
}
