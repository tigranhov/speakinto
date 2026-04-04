#!/bin/bash
# Downloads whisper.cpp CUDA prebuilt binary for Windows x64
# Usage: bash scripts/download-whisper-cuda.sh

set -e

WHISPER_VERSION="v1.8.4"
BINARY_URL="https://github.com/ggml-org/whisper.cpp/releases/download/${WHISPER_VERSION}/whisper-cublas-12.4.0-bin-x64.zip"
BIN_DIR="$(cd "$(dirname "$0")/.." && pwd)/bin/cuda"

mkdir -p "$BIN_DIR"

if [ -f "$BIN_DIR/whisper-cli.exe" ]; then
  echo "whisper.cpp CUDA binary already exists at $BIN_DIR/whisper-cli.exe"
  exit 0
fi

echo "Downloading whisper.cpp ${WHISPER_VERSION} CUDA (cuBLAS 12.4.0) for Windows x64..."
TEMP_ZIP="$BIN_DIR/whisper-cuda-download.zip"
curl -L -o "$TEMP_ZIP" "$BINARY_URL"

echo "Extracting..."
unzip -o "$TEMP_ZIP" -d "$BIN_DIR"
rm "$TEMP_ZIP"

# The archive extracts into a Release/ subdirectory — move files up
if [ -d "$BIN_DIR/Release" ]; then
  mv "$BIN_DIR/Release"/* "$BIN_DIR/"
  rmdir "$BIN_DIR/Release"
fi

# List what was extracted
echo "Contents of $BIN_DIR:"
ls -la "$BIN_DIR"

echo "Done."
