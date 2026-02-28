# MixAgent

LLM-powered live sound engineer. Connects to mixing consoles over the network,
dynamically discovers what's on each channel, and makes real-time mix decisions
through an AI agent loop.

## Architecture

```
Console (X32/Wing/Avantis)
    │ OSC/TCP
    ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  Console     │───▶│  Console     │───▶│  Channel     │
│  Adapter     │    │  Model       │    │  Discovery   │
└──────────────┘    └──────────────┘    └──────┬───────┘
                                               │
                                               ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  Audio       │◀──▶│  Dynamic     │◀──▶│  LLM Review  │
│  Analyser    │    │  Channel Map │    │  Pass        │
└──────┬───────┘    └──────────────┘    └──────────────┘
       │
       ▼
┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│  LLM Decision│───▶│  Approval    │───▶│  Action      │
│  Engine      │    │  Queue       │    │  Executor    │
└──────────────┘    └──────────────┘    └──────────────┘
       │                    │
       ▼                    ▼
┌──────────────┐    ┌──────────────┐
│  Session     │    │  Terminal UI │
│  Memory      │    │  (ftxui)     │
└──────────────┘    └──────────────┘
```

## Supported Consoles

| Console | Protocol | Port | Channels |
|---------|----------|------|----------|
| Behringer X32 / Midas M32 | OSC/UDP | 10023 | 32 |
| Behringer Wing | OSC/UDP | 2222 | 48 |
| Allen & Heath Avantis | TCP | 51325 | 64 |

## Quick Start

```bash
# 1. Build
./scripts/build.sh

# 2. Configure
cp .env.example .env
# Edit .env — add your ANTHROPIC_API_KEY
# Edit config/show.json — set your console IP

# 3. Pre-show check
./scripts/preshow-check.sh

# 4. Run
./scripts/start.sh
```

## How It Works

### Channel Discovery

On startup, the agent:

1. **Full State Sync** — pulls all channel names, faders, EQ, comp, gate settings via OSC
2. **Name Classification** — regex rules match channel names to instrument roles (e.g. "Kick", "Snr", "Vox")
3. **Spectral Classification** — for unnamed channels, analyses frequency content to infer the instrument
4. **Stereo Pair Detection** — identifies L/R pairs from adjacent channels with matching names/roles
5. **LLM Review** — sends the full channel map to the LLM for sanity checking and corrections

The result is a `DynamicChannelMap` that the agent queries by role and group
rather than by hardcoded channel numbers. If an engineer renames a channel
mid-show, it gets reclassified automatically.

### Agent Loop

Two threads run continuously:

- **DSP Thread (50ms)**: reads meters, detects clipping/feedback, handles emergencies
- **LLM Thread (5s)**: builds mix state JSON, asks the LLM for decisions, routes actions through approval

### Safety

Every action passes through:

1. **ActionValidator** — clamps fader deltas to 6dB, EQ boosts to 3dB, HPF to 400Hz max
2. **ApprovalQueue** — urgent actions auto-execute, normal actions wait for engineer approval
3. **Ramped Execution** — fader moves are ramped over 200ms to avoid audible jumps

### Approval Modes

| Mode | Behaviour |
|------|-----------|
| `approve_all` | Every action needs manual approval |
| `auto_urgent` | Immediate/Fast urgency auto-approved, rest queued (default) |
| `auto_all` | Everything auto-approved (demo/testing) |
| `deny_all` | All actions rejected (safe observation mode) |

## Project Structure

```
include/
  console/       IConsoleAdapter, X32/Wing/Avantis adapters, ConsoleModel
  discovery/     ChannelProfile, DynamicChannelMap, classifiers, orchestrator
  llm/           LLMDecisionEngine, ActionSchema, SessionMemory
  analysis/      AudioAnalyser, MeterBridge
  approval/      ApprovalQueue, ApprovalUI
  agent/         SoundEngineerAgent, ActionValidator, ActionExecutor
src/             Implementation files
config/          Console-specific JSON configs
scripts/         Build, run, network setup, pre-show checks
tests/           Unit tests (gtest)
```

## Configuration

### config/show.json

```json
{
    "console_type": "x32",
    "console_ip": "192.168.1.100",
    "console_port": 10023,
    "approval_mode": "auto_urgent",
    "llm_interval_ms": 5000
}
```

### .env

```
ANTHROPIC_API_KEY=sk-ant-xxxxx
OLLAMA_HOST=http://localhost:11434
MIXAGENT_MODEL=claude-sonnet-4-20250514
MIXAGENT_FALLBACK_MODEL=llama3:8b
```

## Building

Requires CMake 3.20+ and a C++20 compiler.

```bash
./scripts/build.sh           # Release build
./scripts/build.sh Debug     # Debug build with symbols
```

Dependencies are fetched automatically via CMake FetchContent:
- [ftxui](https://github.com/ArthurSonzogni/FTXUI) — terminal UI
- [nlohmann/json](https://github.com/nlohmann/json) — JSON
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP client
- [spdlog](https://github.com/gabime/spdlog) — logging

## License

MIT
