include(GoogleTest)

set(mqtls_unit_test_sources
  unit/test_smoke.cpp
  unit/test_error.cpp
  unit/test_config.cpp
  unit/test_transport.cpp
  unit/test_wait.cpp
  unit/test_test_pki.cpp
  unit/test_context.cpp
  unit/test_bio.cpp
  unit/test_framing.cpp
  unit/test_secure_channel_fake.cpp
  unit/test_fake_transport.cpp
  support/test_pki.cpp
  support/fake_transport.cpp
)
set(mqtls_unit_test_libs mqtls::mqtls GTest::gtest GTest::gtest_main)

# mq-module-specific tests: kept out of the executable entirely when the
# module itself is disabled, so a core-only build (MQTLS_BUILD_MQ_TRANSPORT=OFF)
# doesn't need mqueue support to test the core.
if(MQTLS_BUILD_MQ_TRANSPORT)
  list(APPEND mqtls_unit_test_sources
    unit/test_mq_config.cpp
    unit/test_message_queue.cpp
    unit/test_queue_pair.cpp
    support/unique_queue_name.cpp
  )
  list(APPEND mqtls_unit_test_libs mqtls::mq)
endif()

add_executable(mqtls_unit_tests ${mqtls_unit_test_sources})

target_link_libraries(mqtls_unit_tests PRIVATE ${mqtls_unit_test_libs})

# Unit tests reach into mqtls's internal headers (src/util/..., src/mq/...),
# which are not part of the installed, public include path, plus shared
# test support helpers under tests/support/.
target_include_directories(mqtls_unit_tests PRIVATE
  "${PROJECT_SOURCE_DIR}/src"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/support"
)

target_compile_definitions(mqtls_unit_tests
  PRIVATE MQTLS_TEST_PROJECT_VERSION="${PROJECT_VERSION}"
)

mqtls_set_warnings(mqtls_unit_tests)
mqtls_enable_sanitizers(mqtls_unit_tests)
mqtls_enable_coverage(mqtls_unit_tests)
mqtls_apply_sanitizer_test_wrapper(mqtls_unit_tests)

gtest_discover_tests(mqtls_unit_tests)

# Integration tests exercise the whole stack end to end over real POSIX
# message queues (mq::establish() -> SecureChannel); they need the mq
# module and therefore a separate, conditional executable.
if(MQTLS_BUILD_MQ_TRANSPORT)
  add_executable(mqtls_integration_tests
    integration/test_handshake.cpp
    integration/test_send_recv.cpp
    integration/test_concurrency.cpp
    integration/test_failures.cpp
    support/test_pki.cpp
    support/unique_queue_name.cpp
    support/channel_fixture.cpp
  )
  target_link_libraries(mqtls_integration_tests PRIVATE
    mqtls::mqtls
    mqtls::mq
    GTest::gtest
    GTest::gtest_main
  )
  target_include_directories(mqtls_integration_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/support"
  )
  mqtls_set_warnings(mqtls_integration_tests)
  mqtls_enable_sanitizers(mqtls_integration_tests)
  mqtls_enable_coverage(mqtls_integration_tests)
  mqtls_apply_sanitizer_test_wrapper(mqtls_integration_tests)
  gtest_discover_tests(mqtls_integration_tests
    PROPERTIES TIMEOUT 120
  )
endif()

mqtls_add_coverage_target()
if(TARGET coverage)
  # The 'coverage' custom target has no implicit dependency on the test
  # binaries; without this, a clean build runs ctest before they exist and
  # every test reports NOT_BUILT.
  add_dependencies(coverage mqtls_unit_tests)
  if(MQTLS_BUILD_MQ_TRANSPORT)
    add_dependencies(coverage mqtls_integration_tests)
  endif()
endif()
