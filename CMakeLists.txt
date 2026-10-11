cmake_minimum_required(VERSION 3.25)

project(mqtls
  VERSION 0.1.0
  DESCRIPTION "A TLS 1.3 layer over any byte-stream transport, shipping with a POSIX message-queue binding"
  HOMEPAGE_URL "https://github.com/okaynak/tls_over_msgQ"
  LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "mqtls is Linux-only: its wait model relies on mqd_t being a pollable "
    "file descriptor, which is a Linux-specific extension of POSIX mqueues.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)

option(BUILD_SHARED_LIBS "Build mqtls as a shared library" OFF)
option(MQTLS_BUILD_TESTS "Build unit and integration tests" ${PROJECT_IS_TOP_LEVEL})
option(MQTLS_BUILD_EXAMPLES "Build example programs" ${PROJECT_IS_TOP_LEVEL})
option(MQTLS_BUILD_DOCS "Build the Doxygen API reference ('docs' / 'docs-check' targets)" ${DOXYGEN_FOUND})
option(MQTLS_BUILD_MQ_TRANSPORT
  "Build mqtls::mq, the POSIX message-queue transport binding" ON)
option(MQTLS_ENABLE_CLANG_TIDY "Run clang-tidy on the mqtls target while building" OFF)
option(MQTLS_ENABLE_COVERAGE "Build mqtls and its tests with --coverage instrumentation" OFF)
option(MQTLS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(MQTLS_SANITIZER "" CACHE STRING
  "Semicolon-separated sanitizers to enable, e.g. 'address;undefined' or 'thread'")

include(CompilerWarnings)
include(Sanitizers)
include(Coverage)
include(ClangTidy)
include(Doxygen)
include(Dependencies)

find_package(OpenSSL 3.0 REQUIRED)
find_package(Threads REQUIRED)

# tl::expected backs mqtls::Result<T>; the library itself
# depends on it, independent of MQTLS_BUILD_TESTS.
mqtls_fetch_core_dependencies()

# span-lite only installs/exports itself when it is the top-level CMake
# project, not when pulled in via FetchContent as here; read back its
# source directory (set by FetchContent itself, independent of the
# function scope above) so this project can install its headers and fold
# its target into mqtls's own export set below.
FetchContent_GetProperties(span_lite SOURCE_DIR span_lite_SOURCE_DIR)

add_subdirectory(src)

if(MQTLS_BUILD_TESTS)
  enable_testing()
  mqtls_fetch_test_dependencies()
  add_subdirectory(tests)
endif()

if(MQTLS_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(MQTLS_BUILD_DOCS)
  mqtls_add_doxygen_targets()
endif()

# ---------------------------------------------------------------------------
# Install / export
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(mqtls_install_targets mqtls span-lite)
if(MQTLS_BUILD_MQ_TRANSPORT)
  list(APPEND mqtls_install_targets mqtls_mq)
endif()

install(TARGETS ${mqtls_install_targets}
  EXPORT mqtlsTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(DIRECTORY include/mqtls DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# span-lite's own headers, since its CMakeLists.txt won't install them in
# this FetchContent context (see the comment above FetchContent_GetProperties).
install(DIRECTORY "${span_lite_SOURCE_DIR}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT mqtlsTargets
  FILE mqtlsTargets.cmake
  NAMESPACE mqtls::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtls)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/mqtlsConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/mqtlsConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtls)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mqtlsConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/mqtlsConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/mqtlsConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mqtls)
