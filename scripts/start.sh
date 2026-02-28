#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$PROJECT_ROOT/build/mixagent"
CONFIG="${1:-$PROJECT_ROOT/config/show.json}"

if [ ! -f "$BINARY" ]; then
    echo "Binary not found — building first..."
    "$PROJECT_ROOT/scripts/build.sh"
fi

if [ ! -f "$PROJECT_ROOT/.env" ]; then
    echo "WARNING: .env file not found. Copy from .env.example:"
    echo "  cp .env.example .env"
    echo "  # Then add your ANTHROPIC_API_KEY"
    echo ""
fi

echo "Starting MixAgent with config: $CONFIG"
exec "$BINARY" "$CONFIG"
