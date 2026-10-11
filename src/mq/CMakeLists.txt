# The POSIX message-queue transport binding. Deliberately
# a separate target from `mqtls`: this is the only place `rt` (mq_open(3)
# et al.) is linked, so a configure/build with MQTLS_BUILD_MQ_TRANSPORT=OFF
# is a standing proof that the core never depended on it.
add_library(mqtls_mq
  config.cpp
  message_queue.cpp
  queue_pair.cpp
  channel.cpp
)
add_library(mqtls::mq ALIAS mqtls_mq)
# So that install(EXPORT ... NAMESPACE mqtls::) produces mqtls::mq, matching
# the BUILD_INTERFACE alias above, instead of mqtls::mqtls_mq.
set_target_properties(mqtls_mq PROPERTIES EXPORT_NAME mq)

target_include_directories(mqtls_mq
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(mqtls_mq
  PUBLIC
    mqtls::mqtls
  PRIVATE
    rt
)

mqtls_set_warnings(mqtls_mq)
mqtls_enable_sanitizers(mqtls_mq)
mqtls_enable_coverage(mqtls_mq)
mqtls_enable_clang_tidy(mqtls_mq)
