include_guard(GLOBAL)

## mqtls_enable_clang_tidy(<target>)
#
# Sets CXX_CLANG_TIDY on <target> when MQTLS_ENABLE_CLANG_TIDY is ON, so
# clang-tidy runs as part of the normal build for that target.
function(mqtls_enable_clang_tidy target)
  if(NOT MQTLS_ENABLE_CLANG_TIDY)
    return()
  endif()

  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy clang-tidy-14)
  if(NOT CLANG_TIDY_EXECUTABLE)
    message(WARNING "MQTLS_ENABLE_CLANG_TIDY is ON but clang-tidy was not found; skipping ${target}.")
    return()
  endif()

  set_target_properties(${target} PROPERTIES
    CXX_CLANG_TIDY "${CLANG_TIDY_EXECUTABLE}")
endfunction()
