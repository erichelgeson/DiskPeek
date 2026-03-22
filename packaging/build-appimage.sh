#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DIST_DIR="$PROJECT_DIR/dist"

# Detect container runtime
if command -v docker &>/dev/null; then
    CONTAINER_RT=docker
elif command -v podman &>/dev/null; then
    CONTAINER_RT=podman
else
    echo "Error: docker or podman required" >&2
    exit 1
fi

echo "Using $CONTAINER_RT"

# Build the container image (cached)
$CONTAINER_RT build -t diskpeek-appimage -f "$SCRIPT_DIR/Dockerfile.appimage" "$SCRIPT_DIR"

mkdir -p "$DIST_DIR"

# Run the build inside the container
$CONTAINER_RT run --rm \
    -v "$PROJECT_DIR:/src:ro" \
    -v "$DIST_DIR:/dist" \
    diskpeek-appimage bash -c '
set -euo pipefail

# Copy source (read-only mount)
cp -a /src /build/src
cd /build/src

# Build
meson setup builddir --prefix=/usr
ninja -C builddir

# Create AppDir
APPDIR=/build/AppDir
mkdir -p "$APPDIR/usr/bin"
mkdir -p "$APPDIR/usr/share/icons/hicolor/256x256/apps"
mkdir -p "$APPDIR/usr/share/applications"

cp builddir/hfsbrowser "$APPDIR/usr/bin/"

# Create icon from DogCow if available, otherwise generate a placeholder
if [ -f DogCow_from_LaserWriter_8.png ]; then
    convert DogCow_from_LaserWriter_8.png -resize 256x256 \
        "$APPDIR/usr/share/icons/hicolor/256x256/apps/diskpeek.png" 2>/dev/null || \
    cp DogCow_from_LaserWriter_8.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/diskpeek.png"
else
    convert -size 256x256 xc:white -fill black -gravity center -pointsize 48 \
        -annotate 0 "Disk\nPeek" "$APPDIR/usr/share/icons/hicolor/256x256/apps/diskpeek.png"
fi

cp packaging/diskpeek.desktop "$APPDIR/usr/share/applications/"

# Copy font
mkdir -p "$APPDIR/usr/bin/lib/fonts"
cp -r lib/fonts/ChicagoFLF.ttf "$APPDIR/usr/bin/lib/fonts/"

# Run linuxdeploy
linuxdeploy --appimage-extract-and-run \
    --appdir "$APPDIR" \
    --desktop-file "$APPDIR/usr/share/applications/diskpeek.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/diskpeek.png" \
    --exclude-library "libGL.so*" \
    --exclude-library "libGLX.so*" \
    --exclude-library "libGLdispatch.so*" \
    --exclude-library "libEGL.so*" \
    --output appimage

mv Disk_Peek-*.AppImage /dist/diskpeek-x86_64.AppImage
echo "AppImage created: /dist/diskpeek-x86_64.AppImage"
'

echo "Done: $DIST_DIR/diskpeek-x86_64.AppImage"
