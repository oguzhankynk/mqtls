configure_file(version.cpp.in "${CMAKE_CURRENT_BINARY_DIR}/version.cpp" @ONLY)

add_library(mqtls
  "${CMAKE_CURRENT_BINARY_DIR}/version.cpp"
  error.cpp
  config.cpp
  util/wait.cpp
  tls/context.cpp
  tls/bio.cpp
  framing.cpp
  secure_channel.cpp
)
add_library(mqtls::mqtls ALIAS mqtls)

target_compile_features(mqtls PUBLIC cxx_std_17)

target_include_directories(mqtls
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# Deliberately no `rt` here: the core must not need
# POSIX message queues (or anything else mqueue-specific) to build. That is
# `mqtls_mq`'s dependency alone, added in src/mq/CMakeLists.txt.
target_link_libraries(mqtls
  PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    tl::expected
    # The plain (non-namespaced) target, not the nonstd::span-lite alias:
    # span-lite's own CMakeLists.txt only installs/exports itself when built
    # as the top-level project, not as a FetchContent subproject, so this
    # library folds it into mqtls's own install/export instead (see the
    # root CMakeLists.txt install section).
    span-lite
)

mqtls_set_warnings(mqtls)
mqtls_enable_sanitizers(mqtls)
mqtls_enable_coverage(mqtls)
mqtls_enable_clang_tidy(mqtls)

if(MQTLS_BUILD_MQ_TRANSPORT)
  add_subdirectory(mq)
endif()
