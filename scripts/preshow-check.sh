#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${1:-$PROJECT_ROOT/config/show.json}"

echo "=== MixAgent Pre-Show Check ==="
echo ""

# Check binary
if [ -f "$PROJECT_ROOT/build/mixagent" ]; then
    echo "[OK] Binary found"
else
    echo "[!!] Binary not found — run scripts/build.sh first"
fi

# Check .env
if [ -f "$PROJECT_ROOT/.env" ]; then
    echo "[OK] .env file exists"
    if grep -q "ANTHROPIC_API_KEY=sk-" "$PROJECT_ROOT/.env" 2>/dev/null; then
        echo "[OK] Anthropic API key configured"
    else
        echo "[!!] No Anthropic API key — will use Ollama fallback only"
    fi
else
    echo "[!!] No .env file — copy from .env.example"
fi

# Check config
if [ -f "$CONFIG" ]; then
    echo "[OK] Config file: $CONFIG"
    CONSOLE_IP=$(python3 -c "import json; print(json.load(open('$CONFIG'))['console_ip'])" 2>/dev/null || echo "unknown")
    CONSOLE_TYPE=$(python3 -c "import json; print(json.load(open('$CONFIG'))['console_type'])" 2>/dev/null || echo "unknown")
    echo "     Console: $CONSOLE_TYPE @ $CONSOLE_IP"
else
    echo "[!!] Config file not found: $CONFIG"
fi

# Network check
if [ "$CONSOLE_IP" != "unknown" ]; then
    echo ""
    echo "Pinging console at $CONSOLE_IP..."
    if ping -c 1 -W 2 "$CONSOLE_IP" > /dev/null 2>&1; then
        echo "[OK] Console reachable"
    else
        echo "[!!] Console not reachable — check network"
    fi
fi

# Check Ollama (optional)
echo ""
OLLAMA_HOST="${OLLAMA_HOST:-http://localhost:11434}"
if curl -s --connect-timeout 2 "$OLLAMA_HOST/api/tags" > /dev/null 2>&1; then
    echo "[OK] Ollama reachable at $OLLAMA_HOST"
else
    echo "[--] Ollama not running (optional fallback)"
fi

echo ""
echo "=== Check Complete ==="
