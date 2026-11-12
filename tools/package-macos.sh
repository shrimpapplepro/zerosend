#!/usr/bin/env bash
# Build ZeroSend as a self-contained macOS .app bundle.
#
# The bundle carries its own copy of assets/ and points APP_ASSETS_ROOT at it,
# so it keeps working if this repo moves or is deleted. Settings are written to
# ~/Library/Application Support/, never into the bundle -- app bundles should be
# treated as read-only, and a Gatekeeper-translocated one literally is.
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
# Held in variables rather than inline: an apostrophe inside ${VAR:-default}
# breaks bash's parameter expansion.
DEFAULT_APP_NAME="ZeroSend"
# Bundle FILENAME deliberately has no apostrophe -- it would need quoting in
# every shell command that touches it. Finder shows CFBundleDisplayName, so the
# pretty name is not lost.
DEFAULT_BUNDLE_DIR="$HOME/Applications/ZeroSend.app"
APP_NAME="${APP_NAME:-$DEFAULT_APP_NAME}"
BUNDLE_DIR="${BUNDLE_DIR:-$DEFAULT_BUNDLE_DIR}"
BUILD_DIR="${BUILD_DIR:-$REPO_DIR/build/bundle}"
CONFIG_DIR="$HOME/Library/Application Support/ZeroSend"

CONTENTS="$BUNDLE_DIR/Contents"
RESOURCES="$CONTENTS/Resources"

log() { printf '\n==> %s\n' "$*"; }

log "Configuring Release build"
# Reuses the dev build's FetchContent cache so LVGL and nlohmann_json are not
# re-downloaded.
cmake -S "$REPO_DIR" -B "$BUILD_DIR" \
  -G "Ninja Multi-Config" \
  -DCMAKE_CONFIGURATION_TYPES="Debug;Release" \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_OSX_ARCHITECTURES=arm64 \
  -DFETCHCONTENT_BASE_DIR="$REPO_DIR/.cache/darwin-arm64-deps" \
  -DUSE_DESKTOP=ON \
  -DAPP_ASSETS_ROOT="$RESOURCES/assets" \
  -DAPP_CONFIG_FILE="$CONFIG_DIR/zerosend.conf"

log "Building Release"
cmake --build "$BUILD_DIR" --config Release

log "Assembling $BUNDLE_DIR"
rm -rf "$BUNDLE_DIR"
mkdir -p "$CONTENTS/MacOS" "$RESOURCES"

cp "$BUILD_DIR/Release/zerosend" "$CONTENTS/MacOS/zerosend"
cp -R "$REPO_DIR/assets" "$RESOURCES/assets"

# The real executable is a wrapper: a shell that exports MTL_HUD_ENABLED=1
# and the Metal HUD covers a 320x170 window. Finder launches do not inherit it,
# but `open` from a terminal can, so clear it unconditionally.
cat > "$CONTENTS/MacOS/launch" <<'LAUNCHER'
#!/bin/sh
# UNSET, not =0. Metal appears to treat this variable as a presence check, so
# exporting 0 still switched the HUD on -- which is how it kept appearing over
# the 320x170 window despite "disabling" it.
unset MTL_HUD_ENABLED
exec "$(dirname "$0")/zerosend" "$@"
LAUNCHER
chmod +x "$CONTENTS/MacOS/launch" "$CONTENTS/MacOS/zerosend"

VERSION="$(git -C "$REPO_DIR" describe --tags --always --dirty 2>/dev/null || echo 0.1.1)"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>$APP_NAME</string>
    <key>CFBundleDisplayName</key><string>$APP_NAME</string>
    <key>CFBundleIdentifier</key><string>io.github.shrimpapplepro.zerosend</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>0.1.1</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSMinimumSystemVersion</key><string>11.0</string>
    <key>NSSupportsAutomaticGraphicsSwitching</key><true/>
</dict>
</plist>
PLIST

log "Building icon"
ICONSET="$(mktemp -d)/AppIcon.iconset"
mkdir -p "$ICONSET"
# Source art is 200x200, so .icns slots above 256 are upscaled and look soft in
# Finder. Replace assets/icon-source.jpg with larger art and rerun
# tools/make-icon.py if that ever matters.
SRC_ICON="$REPO_DIR/assets/images/zerosend.png"
for size in 16 32 64 128 256 512; do
    sips -z $size $size "$SRC_ICON" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null 2>&1
    sips -z $((size*2)) $((size*2)) "$SRC_ICON" --out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null 2>&1
done
iconutil -c icns "$ICONSET" -o "$RESOURCES/AppIcon.icns" 2>/dev/null || \
    echo "   (iconutil failed; bundle will use the generic icon)"

log "Seeding settings at $CONFIG_DIR"
mkdir -p "$CONFIG_DIR"
if [ ! -f "$CONFIG_DIR/zerosend.conf" ]; then
    cp "$REPO_DIR/config/zerosend.conf" "$CONFIG_DIR/zerosend.conf"
fi

# Ad-hoc signature. Unsigned SDL apps can be killed on launch by Gatekeeper;
# this is enough for a locally-built app and needs no developer account.
codesign --force --deep --sign - "$BUNDLE_DIR" 2>/dev/null || \
    echo "   (codesign failed; first launch may need right-click > Open)"

log "Done: $BUNDLE_DIR"
du -sh "$BUNDLE_DIR" | awk '{print "    size: "$1}'
