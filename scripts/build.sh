#!/usr/bin/env bash
# Build helper for n64emu (Phase 0+)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"
JOBS="${JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

# Optional local SDL2 (e.g. /home/user/deps/sdl2-install)
if [[ -z "${CMAKE_PREFIX_PATH:-}" ]]; then
  for candidate in \
      /home/user/deps/sdl2-install \
      "$ROOT/../deps/sdl2-install" \
      "$HOME/deps/sdl2-install"; do
    if [[ -d "$candidate" ]]; then
      export CMAKE_PREFIX_PATH="$candidate"
      break
    fi
  done
fi

EXTRA_ARGS=()
if [[ "${HEADLESS:-0}" == "1" ]]; then
  EXTRA_ARGS+=(-DN64EMU_HEADLESS=ON)
fi

cmake -S "$ROOT" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DN64EMU_BUILD_TESTS=ON \
  ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"} \
  "${EXTRA_ARGS[@]}" \
  "$@"

cmake --build "$BUILD_DIR" -j"$JOBS"
cmake --build "$BUILD_DIR" --target check

echo "OK — binary: $BUILD_DIR/n64emu"
