find_package(Boost REQUIRED COMPONENTS program_options)
find_package(SQLite3 REQUIRED)
find_package(Protobuf REQUIRED)

if(NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
  add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif()

add_executable(onerss-backend
  src/article_types.cpp
  src/app_server.cpp
  src/feed_fetcher.cpp
  src/feed_refresh_scheduler.cpp
  src/html_sanitizer.cpp
  src/main.cpp
  src/logging.cpp
  src/user_notification.cpp
  src/tree_repository.cpp
  src/sqlite_tree_repository.cpp
  src/sqlite.cpp
  src/user_store.cpp
  src/ssl_util.cpp
  src/signup_server.cpp
)

target_include_directories(onerss-backend
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(onerss-backend
  PRIVATE
    onerss_proto
    logfault
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite3::SQLite3
    Boost::program_options
)

if(TARGET Boost::headers)
  target_link_libraries(onerss-backend PRIVATE Boost::headers)
elseif(TARGET Boost::boost)
  target_link_libraries(onerss-backend PRIVATE Boost::boost)
endif()
