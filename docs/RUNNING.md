# Running MixAgent

## Prerequisites

- **C++20 compiler** (GCC 11+, Clang 14+, or MSVC 19.30+)
- **CMake 3.20+**
- **A mixing console** on the same network (X32, Wing, or Avantis)
- **An LLM backend** — either:
  - An Anthropic API key, or
  - A local [Ollama](https://ollama.ai) instance (fully offline)

## Step 1: Build

```bash
./scripts/build.sh           # Release build
./scripts/build.sh Debug     # Debug build with symbols
```

This uses CMake FetchContent to pull dependencies (ftxui, nlohmann/json,
cpp-httplib, spdlog). First build takes a few minutes; subsequent builds
are fast.

To build and run the tests:

```bash
cd build
cmake .. -DBUILD_TESTS=ON
cmake --build . --parallel
ctest --output-on-failure
```

## Step 2: Configure

### Console connection

Edit `config/show.json`:

```json
{
    "console_type": "x32",
    "console_ip": "192.168.1.100",
    "console_port": 10023
}
```

| Console | `console_type` | Default port |
|---------|---------------|-------------|
| Behringer X32 / Midas M32 | `x32` or `m32` | 10023 (UDP) |
| Behringer Wing | `wing` | 2222 (UDP) |
| Allen & Heath Avantis | `avantis` | 51325 (TCP) |

Your computer must be on the same subnet as the console. Use
`scripts/network-setup.sh eth0 192.168.1.50` for help configuring a
static IP.

### LLM backend

**Option A: Anthropic API (cloud)**

```bash
cp .env.example .env
# Edit .env:
ANTHROPIC_API_KEY=sk-ant-your-key-here
```

**Option B: Ollama (fully local, no internet)**

```bash
# Install Ollama: https://ollama.ai
ollama pull llama3:8b       # or any model you prefer
ollama serve                # start the server
```

Then in `config/show.json`:
```json
{
    "ollama_primary": true
}
```

Or just leave `ANTHROPIC_API_KEY` blank — the agent auto-detects and
switches to Ollama.

**Option C: Both (Anthropic primary, Ollama fallback)**

Set your API key in `.env` and have Ollama running. The agent tries
Anthropic first and falls back to Ollama if the API call fails (network
issues, rate limits, etc.).

## Step 3: Pre-show check

```bash
./scripts/preshow-check.sh
```

This verifies:
- Binary is built
- `.env` exists and has an API key (or Ollama is reachable)
- Console is pingable at the configured IP
- Ollama is running (if applicable)

## Step 4: Run

```bash
./scripts/start.sh                        # uses config/show.json
./scripts/start.sh config/avantis.json    # use a different config

# Or directly:
./build/mixagent config/show.json
```

### Headless mode

For unattended operation (no terminal UI):

```json
{
    "headless": true
}
```

The agent runs as a background process, logs to `mixagent.log`. Stop
with `Ctrl+C` or `kill`.

## Step 5: Using the UI

The terminal UI has two panels and two modes:

### Approval mode (default)

Navigate the approval queue with `j`/`k` or arrow keys. Press `Enter` to
approve, `d` to reject, `A` to approve all, `R` to reject all.

### Chat mode

Press `/` to enter chat mode. Type natural language instructions:

```
> bring up the vocals a bit
> the kick is too boomy, cut some low-mid
> leave the drums alone for now
> what's the current level on the lead vocal?
```

Press `Enter` to send, `Esc` to return to approval mode.

Chat messages become **standing instructions** — they persist in session
memory and influence every subsequent LLM decision until the session ends.

Press `q` to quit the agent.

## Approval modes

Set `"approval_mode"` in your config:

| Mode | Behaviour | Use when |
|------|-----------|----------|
| `approve_all` | Every action queued for manual approval | First time, building trust |
| `auto_urgent` | Emergency actions (clipping/feedback) auto-execute, rest queued | **Default** — normal operation |
| `auto_all` | Everything auto-approved | Demo, testing, or full trust |
| `deny_all` | All actions rejected | Safe observation mode |

## Tuning

| Config key | Default | Effect |
|-----------|---------|--------|
| `dsp_interval_ms` | 50 | How often meters are read and analysis runs |
| `llm_interval_ms` | 5000 | How often the LLM is asked for decisions |
| `meter_refresh_ms` | 50 | Console meter subscription rate |
| `llm_temperature` | 0.3 | Lower = more consistent decisions |
| `llm_max_tokens` | 1024 | Max LLM response length |

## Logs

- **Terminal**: real-time activity in the UI
- **File**: `mixagent.log` (rotating, 5MB x 3 files)
- **Level**: set `MIXAGENT_LOG_LEVEL` in `.env` (`debug`, `info`, `warn`, `error`)
