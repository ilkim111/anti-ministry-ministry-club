#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BUILD_TYPE="${1:-Release}"

echo "Building MixAgent ($BUILD_TYPE)..."

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build . --parallel "$(nproc 2>/dev/null || echo 4)"

echo ""
echo "Build complete: $BUILD_DIR/mixagent"
echo "Run with: ./build/mixagent config/show.json"
