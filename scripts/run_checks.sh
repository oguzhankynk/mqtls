#!/usr/bin/env bash
# Runs every quality gate this project defines, in the same order CI does,
# stopping at the first failure. Run from anywhere; paths are resolved
# relative to this script's location.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT_DIR}"

step() {
  echo
  echo "==> $*"
}

step "clang-format --dry-run"
mapfile -t sources < <(git ls-files '*.hpp' '*.cpp')
clang-format --dry-run --Werror "${sources[@]}"

step "cppcheck"
cppcheck --enable=warning,performance,portability --error-exitcode=1 \
  --inline-suppr --suppress=missingIncludeSystem --std=c++17 \
  -I include -I src -I tests -I examples \
  src include tests examples

step "clang-tidy (preset: tidy)"
cmake --preset tidy
cmake --build --preset tidy

step "Doxygen documentation coverage (preset: debug)"
cmake --preset debug
cmake --build --preset debug --target docs-check

step "Unit + integration tests (preset: debug)"
cmake --build --preset debug
ctest --preset debug

step "Core-only build (mqtls without the mqtls_mq transport module)"
cmake --preset core-only
cmake --build --preset core-only
ctest --preset core-only

step "AddressSanitizer + UndefinedBehaviorSanitizer (preset: asan)"
cmake --preset asan
cmake --build --preset asan
ctest --preset asan

step "ThreadSanitizer (preset: tsan)"
cmake --preset tsan
cmake --build --preset tsan
ctest --preset tsan

step "Coverage (preset: coverage)"
cmake --preset coverage
cmake --build --preset coverage --target coverage

step "Install + find_package(mqtls)"
cmake --preset release
cmake --build --preset release

INSTALL_PREFIX="$(mktemp -d)"
CONSUMER_DIR="$(mktemp -d)"
trap 'rm -rf "${INSTALL_PREFIX}" "${CONSUMER_DIR}"' EXIT

cmake --install build/release --prefix "${INSTALL_PREFIX}"

cat > "${CONSUMER_DIR}/CMakeLists.txt" <<'EOF'
cmake_minimum_required(VERSION 3.25)
project(mqtls_install_check LANGUAGES CXX)
find_package(mqtls CONFIG REQUIRED)
add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE mqtls::mqtls mqtls::mq)
EOF
cat > "${CONSUMER_DIR}/main.cpp" <<'EOF'
#include <mqtls/mqtls.hpp>
#include <mqtls/mq.hpp>
int main() { return mqtls::version() != nullptr ? 0 : 1; }
EOF

cmake -S "${CONSUMER_DIR}" -B "${CONSUMER_DIR}/build" -G Ninja \
  -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX}"
cmake --build "${CONSUMER_DIR}/build"
"${CONSUMER_DIR}/build/consumer"

echo
echo "All checks passed."
