if(ONERSS_BUILD_BACKEND)
  find_package(Protobuf REQUIRED)

  add_library(onerss_proto STATIC onerss.proto)

  target_link_libraries(onerss_proto
    PUBLIC
      protobuf::libprotobuf
  )

  protobuf_generate(
    TARGET onerss_proto
    LANGUAGE cpp
  )

  target_include_directories(onerss_proto
    PUBLIC
      "${CMAKE_CURRENT_BINARY_DIR}"
  )
endif()

if(ONERSS_BUILD_DESKTOP)
  find_package(Qt6 REQUIRED COMPONENTS Protobuf ProtobufTools)

  qt_add_library(onerss_qt_proto STATIC)
  qt_add_protobuf(onerss_qt_proto
    PROTO_FILES
      onerss.proto
  )
endif()
