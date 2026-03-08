#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BUILD_TYPE="${1:-Release}"

echo "Building MixAgent ($BUILD_TYPE)..."

# macOS: detect missing C++ standard library headers and set CXXFLAGS
# This works around broken Xcode Command Line Tools installations where
# headers exist in the SDK but not in the compiler's default search path.
if [[ "$(uname)" == "Darwin" ]]; then
    if ! echo '#include <cstdint>' | c++ -x c++ -fsyntax-only - 2>/dev/null; then
        SDK_PATH="$(xcrun --show-sdk-path 2>/dev/null || true)"
        CXX_HEADERS="$SDK_PATH/usr/include/c++/v1"
        if [[ -d "$CXX_HEADERS" ]]; then
            echo "Detected missing C++ headers — adding SDK include path"
            export CXXFLAGS="${CXXFLAGS:-} -I$CXX_HEADERS"
        else
            echo "ERROR: C++ standard library headers not found." >&2
            echo "Try: xcode-select --install" >&2
            exit 1
        fi
    fi
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$PROJECT_ROOT" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build . --parallel "$(sysctl -n hw.logicalcpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

echo ""
echo "Build complete: $BUILD_DIR/mixagent"
echo "Run with: ./build/mixagent config/show.json"
