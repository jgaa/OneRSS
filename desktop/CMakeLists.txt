
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

if (NOT ANDROID)
  onerss_fetch(safekeeping
    GIT_REPOSITORY https://github.com/jgaa/SafeKeeping.git
    GIT_TAG        main
  )
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS Concurrent Core Gui Network Qml Quick QuickControls2 Widgets Protobuf)

qt_standard_project_setup(REQUIRES 6.10)

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
  src/tray_controller.cpp
  src/tree_types.cpp
  src/user_notification.cpp
)

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
    src/qml/SettingsDialog.qml
    src/qml/SignupDialog.qml
)

target_compile_definitions(onerss-desktop
  PRIVATE
    ONERSS_VERSION="${PROJECT_VERSION}"
)

target_include_directories(onerss-desktop
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(onerss-desktop
  PRIVATE
    onerss_qt_proto
    logfault
    $<$<NOT:$<BOOL:${ANDROID}>>:safekeeping>
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
    Qt6::Widgets
    QCoro6::Core
    QCoro6::Network
)
