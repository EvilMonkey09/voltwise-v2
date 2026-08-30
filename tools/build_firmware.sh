#!/usr/bin/env bash
# Build all edge firmware profiles and copy artifacts for Central flasher + GitHub releases.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW_DIR="$ROOT/edge-firmware"
OUT_DIR="$ROOT/firmware-artifacts/bin"
RELEASE_DIR="$ROOT/firmware-artifacts/release"
VERSION="$(tr -d ' \n\r' < "$ROOT/server/VERSION")"
ENVS="${VOLTWISE_BUILD_ENVS:-esp32dev wt32-eth01 simulation}"

if ! command -v pio >/dev/null 2>&1; then
  echo "PlatformIO (pio) not found. Install: pip install platformio" >&2
  exit 1
fi

mkdir -p "$OUT_DIR" "$RELEASE_DIR"

echo "==> VoltWise firmware build v${VERSION}"

for env in $ENVS; do
  echo "==> Building $env"
  (cd "$FW_DIR" && pio run -e "$env")
  (cd "$FW_DIR" && pio run -e "$env" -t buildfs) || echo "    (littlefs build skipped for $env)"

  BUILD="$FW_DIR/.pio/build/$env"
  DEST="$OUT_DIR/$env"
  mkdir -p "$DEST"

  cp "$BUILD/firmware.bin" "$DEST/"
  [ -f "$BUILD/bootloader.bin" ] && cp "$BUILD/bootloader.bin" "$DEST/"
  [ -f "$BUILD/partitions.bin" ] && cp "$BUILD/partitions.bin" "$DEST/"
  [ -f "$BUILD/littlefs.bin" ] && cp "$BUILD/littlefs.bin" "$DEST/"

  cp "$DEST/firmware.bin" "$RELEASE_DIR/firmware-${env}.bin"
  cp "$DEST/firmware.bin" "$RELEASE_DIR/firmware-${env}-${VERSION}.bin"
  echo "    -> $DEST"
done

echo "==> Done. Flasher binaries in firmware-artifacts/bin/"
