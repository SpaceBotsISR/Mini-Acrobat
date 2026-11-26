#!/usr/bin/env bash
# Builds a self-contained MAgcs.app (Release, arm64, Qt embedded with
# macdeployqt, ad-hoc signed), replaces the installed one and opens it.
# Incremental: only changed sources are recompiled (build dir: gcs/build-release).
#
#   gcs/scripts/build_app.sh                     # -> /Applications/MAgcs.app, then open it
#   gcs/scripts/build_app.sh ~/Applications      # other install folder
#   gcs/scripts/build_app.sh --no-open           # install only
#   gcs/scripts/build_app.sh --clean             # wipe the build dir first
#
# Needs: brew install qt cmake ninja
set -euo pipefail

PREFIX="/Applications"
OPEN_APP=1
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --no-open) OPEN_APP=0 ;;
    --clean) CLEAN=1 ;;
    -h|--help) sed -n '2,12p' "$0"; exit 0 ;;
    -*) echo "unknown option: $arg" >&2; exit 2 ;;
    *) PREFIX="$arg" ;;
  esac
done
GCS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$GCS_DIR/build-release"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: this script builds the macOS .app; on Linux use cmake directly" >&2
  exit 1
fi
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "error: shell is not arm64 (Rosetta?). Open a native terminal." >&2
  exit 1
fi
if [[ -n "${CONDA_PREFIX:-}" ]]; then
  echo "error: a conda/micromamba env is active ($CONDA_PREFIX)." >&2
  echo "       It ships its own Qt; deactivate it (e.g. leave ros_jazzy) and re-run." >&2
  exit 1
fi
if ! command -v brew >/dev/null; then
  echo "error: Homebrew not found" >&2
  exit 1
fi
QT_PREFIX="$(brew --prefix qt)"
GENERATOR=()
if command -v ninja >/dev/null; then
  GENERATOR=(-G Ninja)
fi

if [[ $CLEAN -eq 1 ]]; then
  rm -rf "$BUILD_DIR"
fi

cmake -S "$GCS_DIR" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DMAGCS_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR"

# Quit a running MAgcs first (normal quit: it sends its best-effort disarm).
if pgrep -xq MAgcs; then
  echo "Quitting the running MAgcs..."
  osascript -e 'quit app "MAgcs"' >/dev/null 2>&1 || true
  for _ in $(seq 1 50); do
    pgrep -xq MAgcs || break
    sleep 0.1
  done
  pkill -x MAgcs 2>/dev/null || true
fi

# Replace the previous install (macdeployqt would otherwise mix old and new files).
rm -rf "$PREFIX/MAgcs.app"
cmake --install "$BUILD_DIR" --prefix "$PREFIX"

echo
echo "Installed: $PREFIX/MAgcs.app"
if [[ $OPEN_APP -eq 1 ]]; then
  open "$PREFIX/MAgcs.app"
fi
