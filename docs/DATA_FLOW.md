# How Sound Data Reaches the LLM

The LLM never receives raw audio. It receives a **structured JSON snapshot**
of the mix state — numeric parameters, meter readings, and channel
classifications — assembled from console data every 5 seconds.

## The full data path

```
 Mixing Console (hardware)
      │
      │  OSC/UDP (X32, Wing) or TCP (Avantis)
      │  ← bidirectional: we read state AND write changes back
      │
      ▼
 ┌──────────────┐
 │ Console      │  Speaks the console's native protocol.
 │ Adapter      │  Sends/receives OSC messages or TCP frames.
 │              │  Fires callbacks on parameter changes and meter data.
 └──────┬───────┘
        │  onParameterUpdate(ch, param, value)
        │  onMeterUpdate(ch, rmsDB, peakDB)
        ▼
 ┌──────────────┐
 │ Console      │  Thread-safe central state model.
 │ Model        │  Stores every channel's current fader, mute, EQ, comp,
 │              │  gate, send levels, meter readings, and spectral data.
 │              │  Updated continuously by adapter callbacks.
 └──────┬───────┘
        │
        ├──────────────────────────────────────┐
        │                                      │
        ▼                                      ▼
 ┌──────────────┐                       ┌──────────────┐
 │ Audio        │  (DSP thread, 50ms)   │ Dynamic      │
 │ Analyser     │                       │ Channel Map  │
 │              │  Reads meter values,  │              │  Maps ch numbers
 │              │  detects clipping,    │              │  to instrument roles
 │              │  feedback risk, and   │              │  (Kick, Snare, Vox…)
 │              │  frequency masking.   │              │  Updated by discovery
 └──────────────┘                       └──────┬───────┘
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │ MeterBridge  │  Combines ConsoleModel
                                        │              │  + ChannelMap into a
                                        │              │  JSON snapshot for the
                                        │              │  LLM to read.
                                        └──────┬───────┘
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │ LLM Decision │  (LLM thread, 5s)
                                        │ Engine       │  Sends JSON to
                                        │              │  Anthropic or Ollama.
                                        │              │  Parses response into
                                        │              │  typed MixActions.
                                        └──────────────┘
```

## What the LLM actually receives

Every 5 seconds, `MeterBridge::buildMixState()` constructs a JSON object
like this:

```json
{
  "channels": [
    {
      "index": 1,
      "name": "Kick",
      "role": "Kick",
      "group": "drums",
      "fader": 0.72,
      "muted": false,
      "pan": 0.0,
      "rms_db": -18.3,
      "peak_db": -6.1,
      "has_signal": true,
      "hpf_hz": 60,
      "eq": [
        {"band": 2, "freq": 400.0, "gain": -3.5, "q": 2.0}
      ],
      "comp": {
        "threshold": -12.0,
        "ratio": 4.0,
        "attack": 10.0,
        "release": 100
      }
    },
    {
      "index": 3,
      "name": "Vox",
      "role": "LeadVocal",
      "group": "vocals",
      "fader": 0.80,
      "muted": false,
      "rms_db": -14.2,
      "peak_db": -3.8,
      "has_signal": true,
      "stereo_pair": null
    }
  ],
  "engineer_instructions": [
    "bring up the vocals a bit",
    "leave the drums alone"
  ]
}
```

Key points:

- **Only channels with signal or a name** are included (silent unnamed
  channels are skipped to save tokens)
- **EQ bands are only included if gain != 0** (flat bands omitted)
- **Compressor/gate only included if enabled**
- **`rms_db` and `peak_db`** come from the console's own metering,
  updated every 50ms by the adapter's meter subscription
- **`role` and `group`** come from the discovery layer (name
  classification + spectral analysis)
- **`engineer_instructions`** are any chat messages the engineer has
  typed during this session

## What the LLM does NOT receive

- Raw audio samples (PCM, WAV, etc.)
- Spectral FFT data directly
- Waveform images
- Anything from the PA system or room microphones

The LLM works entirely from **console state + meter readings**. This is
the same information a sound engineer would see on the console's screen
and meter bridge — fader positions, meter levels, EQ curves, compressor
settings — just in JSON form.

## How meter data gets to ConsoleModel

### X32 / M32 (OSC/UDP)

1. Agent sends `/meters /0` to subscribe to input meters
2. Console sends back a meter blob every ~50ms containing 32 float values
   (one per input channel, 0.0–1.0 normalized)
3. `X32Adapter::handleMeterMessage()` converts to dBFS:
   `dBFS = 20 * log10(level)`
4. Fires `onMeterUpdate(ch, dBFS, dBFS)` callback
5. `ConsoleModel::updateMeter()` stores the values

The subscription must be renewed every 10 seconds (`/xremote` keepalive).

### Wing (OSC/UDP)

Same approach — `/$meters` subscription, similar blob format, 48 channels.

### Avantis (TCP)

1. Agent sends command `0x10` with subscribe flag
2. Console streams meter data as binary frames
3. `AvantisAdapter::parseMessage()` extracts per-channel float levels
4. Same conversion to dBFS and callback path

## How the LLM responds

The LLM returns a JSON array of actions:

```json
[
  {
    "action": "set_fader",
    "channel": 3,
    "role": "LeadVocal",
    "value": 0.85,
    "urgency": "normal",
    "reason": "vocal sitting slightly below the mix, +2dB"
  },
  {
    "action": "set_eq",
    "channel": 1,
    "role": "Kick",
    "value": 350,
    "value2": -2.5,
    "value3": 1.5,
    "band": 2,
    "urgency": "low",
    "reason": "cut boxiness to reduce masking with bass"
  }
]
```

Each action is parsed into a typed `MixAction`, validated (clamped to safe
limits), routed through the approval queue, and executed back to the
console via the adapter.

## The analysis layer (not LLM)

The `AudioAnalyser` runs on the DSP thread (50ms), separate from the LLM.
It handles time-critical detection:

- **Clipping**: `peak > -0.5 dBFS` → immediate fader reduction (bypasses
  LLM entirely)
- **Feedback risk**: sustained high RMS with low crest factor (peak ≈ RMS
  means a pure tone, likely feedback)
- **Masking**: compares bass/mid energy between channel pairs (e.g. kick
  vs bass guitar)

These detections either trigger immediate safety actions or get logged
as warnings for the LLM to consider in its next cycle.

## Timing summary

| Component | Rate | What it does |
|-----------|------|-------------|
| Console meter subscription | 50ms | Raw level data from hardware |
| DSP thread (`AudioAnalyser`) | 50ms | Clipping/feedback detection, spectral analysis |
| Console keepalive (`tick()`) | 8–10s | Maintains OSC/TCP subscription |
| LLM decision cycle | 5s | Full mix state → LLM → actions |
| Session memory snapshot | 60s | Periodic mix state archived for context |
| Chat response | On demand | Immediate LLM call when engineer types |
