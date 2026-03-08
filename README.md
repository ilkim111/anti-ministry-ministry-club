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
electron/        Desktop app (Electron)
  main.js        Main process — backend lifecycle, IPC handlers
  preload.js     Context bridge API exposed to renderer
  renderer/      HTML/CSS/JS frontend (config, approval, chat)
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
MIXAGENT_MODEL=claude-sonnet-4-20250514
```

Local model settings (Ollama host, model name, primary toggle) are configured
in `show.json` or via the desktop app's **Local Model** panel.

## Building

Requires CMake 3.20+ and a C++20 compiler.

```bash
# Install dependencies (macOS)
brew install cmake

./scripts/build.sh           # Release build
./scripts/build.sh Debug     # Debug build with symbols
```

> **macOS troubleshooting:** If the build fails with `'cstdint' file not found` or
> similar missing C++ headers, the build script will auto-detect this and add the
> SDK include path. If that doesn't work, reinstall the Command Line Tools:
> ```bash
> sudo rm -rf /Library/Developer/CommandLineTools
> xcode-select --install
> ```

Dependencies are fetched automatically via CMake FetchContent:
- [ftxui](https://github.com/ArthurSonzogni/FTXUI) — terminal UI
- [nlohmann/json](https://github.com/nlohmann/json) — JSON
- [cpp-httplib](https://github.com/yhirose/cpp-httplib) — HTTP client
- [spdlog](https://github.com/gabime/spdlog) — logging

## Electron Desktop App

A desktop GUI that wraps the C++ backend with a modern interface for config
management, the approval workflow, and an engineer chat panel.

### Setup

```bash
cd electron
npm install
```

### Development

```bash
# Build the C++ backend first
npm run build-backend

# Launch the desktop app
npm start
```

### Features

- **Config Editor** — edit `show.json`, console settings, approval modes, LLM
  settings, and `.env` variables from the GUI. Switch between console configs
  (X32, Wing, Avantis) with a dropdown.
- **Approval Queue** — view pending LLM mix actions with urgency badges,
  approve/reject individually or in batch, change approval mode on the fly.
- **Engineer Chat** — send real-time mix instructions to the LLM agent
  ("bring up the vocals", "more reverb on snare") and see responses.
- **Backend Logs** — live log stream from the C++ engine.
- **Packaging** — build distributable apps for macOS, Windows, and Linux via
  electron-builder.

### Packaging

```bash
npm run dist            # Build for current platform
npm run dist:mac        # macOS (.dmg, .zip)
npm run dist:win        # Windows (.exe, portable)
npm run dist:linux      # Linux (.AppImage, .deb)
```

Or from the project root via `make`:

```bash
make dist               # Build backend + package for current platform
make dist-mac           # Backend + macOS .dmg/.zip
make dist-win           # Backend + Windows NSIS/.portable
make dist-linux         # Backend + Linux AppImage/.deb
```

### Distribution

#### For end users

Users install MixAgent like any native app:

| Platform | Installer | Install method |
|----------|-----------|----------------|
| macOS | `MixAgent-x.x.x.dmg` | Open DMG, drag to Applications |
| macOS (CI) | `MixAgent-x.x.x-mac.zip` | Unzip, move to Applications |
| Windows | `MixAgent-Setup-x.x.x.exe` | Run installer (choose install dir) |
| Windows (portable) | `MixAgent-x.x.x.exe` | Run directly, no install needed |
| Linux | `MixAgent-x.x.x.AppImage` | `chmod +x`, run directly |
| Linux (Debian) | `mixagent_x.x.x_amd64.deb` | `sudo dpkg -i` or double-click |

#### Publishing a release

1. Bump the version in `electron/package.json`
2. Build and publish via GitHub Releases:
   ```bash
   cd electron
   export GH_TOKEN=ghp_xxxxx
   npm run dist -- --publish always
   ```
3. Go to GitHub Releases, review the draft, and click **Publish**
4. Users with the app installed will receive the update automatically

For CI/CD, add a GitHub Actions workflow that triggers on version tags:

```yaml
# .github/workflows/release.yml
on:
  push:
    tags: ['v*']

jobs:
  build:
    strategy:
      matrix:
        os: [macos-latest, windows-latest, ubuntu-latest]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with: { node-version: 20 }
      - name: Build backend
        run: ./scripts/build.sh
      - name: Build & publish Electron app
        env:
          GH_TOKEN: ${{ secrets.GITHUB_TOKEN }}
        run: cd electron && npm ci && npm run dist -- --publish always
```

### Auto-Updates

MixAgent uses [electron-updater](https://www.electron.build/auto-update) to push
updates to users automatically. The update flow:

```
GitHub Release published
        │
        ▼
App checks for updates (on launch + every 30 min)
        │
        ▼
User sees banner: "Update available: v2.1.0"
        │
  [Download] button
        │
        ▼
Progress bar shows download progress
        │
        ▼
"Update downloaded. Restart to apply."
        │
  [Restart & Update] button
        │
        ▼
App restarts with new version
```

**How it works:**
- On launch (and every 30 minutes), the app checks GitHub Releases for a newer
  version via `electron-updater`
- If an update is found, a banner appears at the top of the window
- The user clicks **Download** — the update downloads in the background with a
  progress bar
- Once downloaded, the user clicks **Restart & Update** — the app stops the
  backend, quits, installs the update, and relaunches
- Updates are differential on macOS (blockmap-based) for faster downloads

**Update channels:**
- `releaseType: "release"` — stable releases only (default)
- `releaseType: "prerelease"` — include pre-releases / beta builds

**For fully offline / air-gapped environments:**
Users can manually download the latest installer from GitHub Releases and
install over the existing version. Config files and `.env` are stored in the
OS user data directory and persist across updates.

**User data locations (preserved across updates):**

| Platform | Path |
|----------|------|
| macOS | `~/Library/Application Support/MixAgent/` |
| Windows | `%APPDATA%\MixAgent\` |
| Linux | `~/.config/MixAgent/` |

### Architecture

The Electron app communicates with the C++ backend via `stdin`/`stdout` JSON
messages. The main process spawns `MixAgent --headless` and relays structured
events (approval requests, meter updates, LLM responses) to the renderer via
IPC. Engineer commands flow back through `stdin`.

```
┌─────────────────────────────────────────────┐
│  Electron Renderer (HTML/CSS/JS)            │
│  ┌──────────┬──────────────┬──────────┐     │
│  │  Config   │  Approval    │  Chat    │     │
│  │  Editor   │  Queue       │  Panel   │     │
│  └──────────┴──────────────┴──────────┘     │
│                    │ IPC                     │
├─────────────────────────────────────────────┤
│  Electron Main Process                      │
│      │ stdin/stdout JSON                    │
├─────────────────────────────────────────────┤
│  MixAgent C++ Backend (--headless)          │
│  DSP ─── LLM ─── Approval ─── Execution    │
└─────────────────────────────────────────────┘
```

## License

MIT
