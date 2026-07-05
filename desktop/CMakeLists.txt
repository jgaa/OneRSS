
if (LINUX)
  set(QCORO_WITH_QTDBUS ON CACHE BOOL "" FORCE)
  set(SAFEKEEPING_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SAFEKEEPING_ENABLE_KWALLET ON CACHE BOOL "" FORCE)
else()
  set(QCORO_WITH_QTDBUS OFF CACHE BOOL "" FORCE)
endif()
set(QCORO_WITH_QTNETWORK ON CACHE BOOL "" FORCE)
set(QCORO_WITH_QTWEBSOCKETS OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTQUICK OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QML OFF CACHE BOOL "" FORCE)
set(QCORO_WITH_QTTEST OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(QCORO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

onerss_fetch(qcoro
  GIT_REPOSITORY https://github.com/qcoro/qcoro.git
  GIT_TAG        main
)

onerss_fetch(safekeeping
  GIT_REPOSITORY https://github.com/jgaa/SafeKeeping.git
  GIT_TAG        main
)

find_package(Qt6 6.10 REQUIRED COMPONENTS Concurrent Core Gui Network Qml Quick QuickControls2 Svg Widgets Protobuf)

qt_standard_project_setup(REQUIRES 6.10)

if (ANDROID)
  include(FetchContent)
  FetchContent_Declare(
    android_openssl
    DOWNLOAD_EXTRACT_TIMESTAMP true
    URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
  )
  FetchContent_MakeAvailable(android_openssl)
  include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
else() # Not android
  set(TRAY_CONTROLLER_SRC src/tray_controller.cpp)
endif()

qt_add_executable(onerss-desktop
  src/article_list_model.cpp
  src/article_types.cpp
  src/app_info.cpp
  src/app_client.cpp
  src/feed_tree_model.cpp
  src/feed_title_resolver.cpp
  src/main.cpp
  src/logging.cpp
  src/main_view_model.cpp
  src/secret_store.cpp
  src/signup_client.cpp
  src/signup_view_model.cpp
  ${TRAY_CONTROLLER_SRC}
  src/tree_types.cpp
  src/user_notification.cpp
)

if (ANDROID)
  add_android_openssl_libraries(onerss-desktop)
endif()

set_target_properties(onerss-desktop PROPERTIES
   QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")

qt_add_qml_module(onerss-desktop
  URI OneRSS
  VERSION 1.0
  QML_FILES
    src/qml/Main.qml
    src/qml/AboutDialog.qml
    src/qml/ArticleListPane.qml
    src/qml/ArticlePreviewPane.qml
    src/qml/FeedPane.qml
    src/qml/GeneralSettings.qml
    src/qml/LoggingSettings.qml
    src/qml/MaterialIcon.qml
    src/qml/SettingsDialog.qml
    src/qml/SignupDialog.qml
  RESOURCES
    ../fonts/MaterialSymbolsOutlined[FILL,GRAD,opsz,wght].ttf
    ../fonts/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf
    ../fonts/MaterialSymbolsSharp[FILL,GRAD,opsz,wght].ttf
)

set_source_files_properties(
  ../icons/onerss.svg
  PROPERTIES
    QT_RESOURCE_ALIAS onerss.svg
)

set_source_files_properties(
  ../flatpak/org.jgaa.OneRSS-256.png
  PROPERTIES
    QT_RESOURCE_ALIAS org.jgaa.OneRSS-256.png
)

set_source_files_properties(
  ../icons/onerss.svg
  PROPERTIES
    QT_RESOURCE_ALIAS onerss.svg
)

set_source_files_properties(
  ../flatpak/org.jgaa.OneRSS-256.png
  PROPERTIES
    QT_RESOURCE_ALIAS org.jgaa.OneRSS-256.png
)

set_source_files_properties(
  ../icons/onerss.svg
  PROPERTIES
    QT_RESOURCE_ALIAS onerss.svg
)

set_source_files_properties(
  ../flatpak/org.jgaa.OneRSS-256.png
  PROPERTIES
    QT_RESOURCE_ALIAS org.jgaa.OneRSS-256.png
)

qt_add_resources(onerss-desktop "onerss_icons"
  PREFIX
    "/icons"
  FILES
    ../icons/onerss.svg
    ../flatpak/org.jgaa.OneRSS-256.png
)

target_compile_definitions(onerss-desktop
  PRIVATE
    ONERSS_VERSION="${PROJECT_VERSION}"
)

target_include_directories(onerss-desktop
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

set_target_properties(onerss-desktop PROPERTIES
  QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android"
)

if (ANDROID)
  set_target_properties(onerss-desktop PROPERTIES
    QT_ANDROID_APP_NAME "OneRSS"
  )
endif()

target_link_libraries(onerss-desktop
  PRIVATE
    onerss_qt_proto
    logfault
    safekeeping
    $<$<NOT:$<BOOL:${ANDROID}>>:OpenSSL::SSL>
    $<$<NOT:$<BOOL:${ANDROID}>>:OpenSSL::Crypto>
    Qt6::Concurrent
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::Protobuf
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    $<$<NOT:$<BOOL:${ANDROID}>>:Qt6::Widgets>
    QCoro6::Core
    QCoro6::Network
)

qt_finalize_target(onerss-desktop)
