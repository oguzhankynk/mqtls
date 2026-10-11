include_guard(GLOBAL)
include(FetchContent)

## mqtls_fetch_core_dependencies()
#
# Fetches dependencies the mqtls library itself needs at all times:
# tl::expected backs mqtls::Result<T> until the project can move to
# std::expected (C++23). Header-only; pinned to an exact tag for
# reproducible builds.
function(mqtls_fetch_core_dependencies)
  FetchContent_Declare(
    tl_expected
    GIT_REPOSITORY https://github.com/TartanLlama/expected.git
    GIT_TAG v1.1.0
    GIT_SHALLOW TRUE
    SYSTEM)
  set(EXPECTED_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(EXPECTED_BUILD_PACKAGE OFF CACHE BOOL "" FORCE)
  set(EXPECTED_BUILD_PACKAGE_DEB OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(tl_expected)

  # The project targets C++17, which has no std::span; span-lite's
  # nonstd::span (aliased as mqtls::span, include/mqtls/span.hpp) stands in
  # for it. Header-only; pinned to an exact tag for reproducible builds.
  FetchContent_Declare(
    span_lite
    GIT_REPOSITORY https://github.com/martinmoene/span-lite.git
    GIT_TAG v0.11.0
    GIT_SHALLOW TRUE
    SYSTEM)
  set(SPAN_LITE_OPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(SPAN_LITE_OPT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(span_lite)
endfunction()

## mqtls_fetch_test_dependencies()
#
# Fetches GoogleTest/GoogleMock, used by tests/ only.
# Pinned to an exact tag for reproducible builds.
function(mqtls_fetch_test_dependencies)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
    GIT_SHALLOW TRUE
    SYSTEM)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endfunction()
