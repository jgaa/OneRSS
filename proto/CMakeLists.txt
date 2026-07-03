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
