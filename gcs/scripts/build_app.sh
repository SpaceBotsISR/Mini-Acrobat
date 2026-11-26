#!/usr/bin/env bash
# Builds a self-contained MAgcs.app (Release, arm64, Qt embedded with
# macdeployqt, ad-hoc signed) and installs it.
#
#   gcs/scripts/build_app.sh                  # -> /Applications/MAgcs.app
#   gcs/scripts/build_app.sh ~/Applications   # -> ~/Applications/MAgcs.app
#
# Needs: brew install qt cmake ninja
set -euo pipefail

PREFIX="${1:-/Applications}"
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

cmake -S "$GCS_DIR" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DMAGCS_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR"

# Replace any previous install (macdeployqt would otherwise mix old and new files).
rm -rf "$PREFIX/MAgcs.app"
cmake --install "$BUILD_DIR" --prefix "$PREFIX"

echo
echo "Installed: $PREFIX/MAgcs.app"
echo "Open it from Launchpad / Spotlight, or: open \"$PREFIX/MAgcs.app\""
