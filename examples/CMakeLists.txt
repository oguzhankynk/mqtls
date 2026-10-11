# Examples use mqtls::mq (the POSIX message-queue transport), so they only
# build when that module does.
if(NOT MQTLS_BUILD_MQ_TRANSPORT)
  return()
endif()

add_library(mqtls_examples_common STATIC common.cpp)
target_link_libraries(mqtls_examples_common PUBLIC mqtls::mqtls)
mqtls_set_warnings(mqtls_examples_common)

foreach(example_name echo_peer chat_peer multithread_peer)
  add_executable(${example_name} ${example_name}.cpp)
  target_link_libraries(${example_name} PRIVATE mqtls::mqtls mqtls::mq mqtls_examples_common)
  mqtls_set_warnings(${example_name})
endforeach()
