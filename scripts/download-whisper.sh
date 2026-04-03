#!/bin/bash
# Downloads whisper.cpp prebuilt binary for Windows x64
# Usage: bash scripts/download-whisper.sh

set -e

WHISPER_VERSION="v1.8.4"
BINARY_URL="https://github.com/ggml-org/whisper.cpp/releases/download/${WHISPER_VERSION}/whisper-bin-x64.zip"
BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/bin"

mkdir -p "$BIN_DIR"

if [ -f "$BIN_DIR/main.exe" ]; then
  echo "whisper.cpp binary already exists at $BIN_DIR/main.exe"
  exit 0
fi

echo "Downloading whisper.cpp ${WHISPER_VERSION} for Windows x64..."
TEMP_ZIP="$BIN_DIR/whisper-download.zip"
curl -L -o "$TEMP_ZIP" "$BINARY_URL"

echo "Extracting..."
unzip -o "$TEMP_ZIP" -d "$BIN_DIR"
rm "$TEMP_ZIP"

# List what was extracted
echo "Contents of $BIN_DIR:"
ls -la "$BIN_DIR"

echo "Done."
