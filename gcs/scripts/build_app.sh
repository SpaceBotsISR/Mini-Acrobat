#!/usr/bin/env bash
# Builds a self-contained MAgcs.app (Release, arm64, Qt embedded, ad-hoc
# signed), replaces the installed one and opens it.
# Incremental: only changed sources are recompiled (build dir: gcs/build-release).
#
#   gcs/scripts/build_app.sh                     # -> /Applications/MAgcs.app, then open it
#   gcs/scripts/build_app.sh ~/Applications      # other install folder
#   gcs/scripts/build_app.sh --no-open           # install only
#   gcs/scripts/build_app.sh --clean             # wipe the build dir first
#
# Needs: brew install qt cmake ninja
#
# Why not plain macdeployqt: Homebrew splits Qt into one keg per module, and
# macdeployqt then fails on plugins MAgcs does not use (PDF, SVG, WebP,
# virtual keyboard) whose frameworks live in other kegs. So the bundle gets
# only the Cocoa platform plugin, and Homebrew's lib/ is on the search path.
set -euo pipefail

PREFIX="/Applications"
OPEN_APP=1
CLEAN=0
for arg in "$@"; do
  case "$arg" in
    --no-open) OPEN_APP=0 ;;
    --clean) CLEAN=1 ;;
    -h|--help) sed -n '2,10p' "$0"; exit 0 ;;
    -*) echo "unknown option: $arg" >&2; exit 2 ;;
    *) PREFIX="$arg" ;;
  esac
done

GCS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$GCS_DIR/build-release"
STAGE_DIR="$BUILD_DIR/stage"

die() { echo "error: $*" >&2; exit 1; }

[[ "$(uname -s)" == "Darwin" ]] || die "this script builds the macOS .app; on Linux use cmake directly"
[[ "$(uname -m)" == "arm64" ]] || die "shell is not arm64 (Rosetta?). Open a native terminal."
if [[ -n "${CONDA_PREFIX:-}" ]]; then
  die "a conda/micromamba env is active ($CONDA_PREFIX). It ships its own Qt; leave it (e.g. ros_jazzy) and re-run."
fi
command -v brew >/dev/null || die "Homebrew not found"

BREW_PREFIX="$(brew --prefix)"
QT_PREFIX="$(brew --prefix qt)"
MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
QT_PLUGINS="$QT_PREFIX/share/qt/plugins"
[[ -x "$MACDEPLOYQT" ]] || die "macdeployqt not found at $MACDEPLOYQT (brew install qt)"
[[ -f "$QT_PLUGINS/platforms/libqcocoa.dylib" ]] || die "Qt Cocoa plugin not found in $QT_PLUGINS"
MACOS_MAJOR="$(sw_vers -productVersion | cut -d. -f1)"

GENERATOR=()
command -v ninja >/dev/null && GENERATOR=(-G Ninja)

# ---- build ----------------------------------------------------------------------
if [[ $CLEAN -eq 1 ]]; then
  rm -rf "$BUILD_DIR"
fi
cmake -S "$GCS_DIR" -B "$BUILD_DIR" "${GENERATOR[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MAJOR.0" \
  -DMAGCS_BUILD_TESTS=OFF >/dev/null
cmake --build "$BUILD_DIR"

# ---- make the bundle self-contained, in a staging copy ---------------------------
echo "Bundling Qt..."
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"
APP="$STAGE_DIR/MAgcs.app"
ditto "$BUILD_DIR/MAgcs.app" "$APP"

PLUGIN="$APP/Contents/PlugIns/platforms/libqcocoa.dylib"
mkdir -p "$(dirname "$PLUGIN")"
cp "$QT_PLUGINS/platforms/libqcocoa.dylib" "$PLUGIN"   # follows the Homebrew symlink
chmod u+w "$PLUGIN"

DEPLOY_LOG="$BUILD_DIR/macdeployqt.log"
"$MACDEPLOYQT" "$APP" \
  -no-plugins \
  -always-overwrite \
  -executable="$PLUGIN" \
  -libpath="$BREW_PREFIX/lib" >"$DEPLOY_LOG" 2>&1 || {
  cat "$DEPLOY_LOG" >&2
  die "macdeployqt failed"
}
if grep -q "^ERROR" "$DEPLOY_LOG"; then
  grep "^ERROR" "$DEPLOY_LOG" >&2
  die "macdeployqt reported errors (full log: $DEPLOY_LOG)"
fi

# Plugins are looked up in Contents/PlugIns.
printf '[Paths]\nPlugins = PlugIns\n' >"$APP/Contents/Resources/qt.conf"

# Nothing in the bundle may still load from Homebrew. A dylib's own install
# id (first otool -L entry, `otool -D`) is not a load reference: skip it.
homebrew_refs() {
  local bin id
  find "$APP" -type f -print0 | xargs -0 file | grep 'Mach-O' | cut -d: -f1 |
    while read -r bin; do
      id="$(otool -D "$bin" | sed -n 2p)"
      otool -L "$bin" | tail -n +2 | awk '{print $1}' \
        | grep -vxF "${id:-<no-id>}" \
        | grep -E "^($BREW_PREFIX|/usr/local/(opt|Cellar))" \
        | sed "s|^|${bin#"$APP"/} -> |" || true
    done
}
LEFTOVER="$(homebrew_refs)"
if [[ -n "$LEFTOVER" ]]; then
  echo "$LEFTOVER" >&2
  die "the bundle still loads libraries from Homebrew"
fi

codesign --force --deep --sign - "$APP"
codesign --verify --deep "$APP"

# ---- replace the installed app ------------------------------------------------------
# Quit a running MAgcs first (normal quit: it sends its best-effort disarm).
if pgrep -x MAgcs >/dev/null; then
  echo "Quitting the running MAgcs..."
  osascript -e 'quit app "MAgcs"' >/dev/null 2>&1 || true
  for _ in $(seq 1 50); do
    pgrep -x MAgcs >/dev/null || break
    sleep 0.1
  done
  pkill -x MAgcs 2>/dev/null || true
fi

mkdir -p "$PREFIX"
rm -rf "$PREFIX/MAgcs.app"
ditto "$APP" "$PREFIX/MAgcs.app"

echo "Installed: $PREFIX/MAgcs.app ($(du -sh "$PREFIX/MAgcs.app" | cut -f1))"
if [[ $OPEN_APP -eq 1 ]]; then
  open "$PREFIX/MAgcs.app"
fi
