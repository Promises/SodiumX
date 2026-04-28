#!/bin/bash
set -e

IMAGE_NAME="sodiumx-xbox"
OUT_DIR="$(pwd)/out"

echo "Building nxdk toolchain and SodiumX..."
docker build --platform linux/amd64 -f Dockerfile.xbox --target builder -t "$IMAGE_NAME" .

echo "Extracting build artifacts..."
mkdir -p "$OUT_DIR"
docker run --rm --platform linux/amd64 -v "$OUT_DIR":/out "$IMAGE_NAME" \
    sh -c "cp /usr/src/sodiumx/bin/default.xbe /usr/src/sodiumx/SodiumX.iso /out/"

echo "Done! Artifacts in $OUT_DIR:"
ls -lh "$OUT_DIR"/default.xbe "$OUT_DIR"/SodiumX.iso
