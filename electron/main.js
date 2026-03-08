const { app, BrowserWindow, ipcMain, dialog, systemPreferences } = require('electron');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');
const { autoUpdater } = require('electron-updater');

let mainWindow;
let backendProcess = null;

// Resolve paths — in dev use project root, in packaged use resources
function resolveConfigDir() {
  if (app.isPackaged) {
    return path.join(process.resourcesPath, 'config');
  }
  return path.join(__dirname, '..', 'config');
}

function resolveBackendPath() {
  const bin = process.platform === 'win32' ? 'mixagent.exe' : 'mixagent';
  if (app.isPackaged) {
    return path.join(process.resourcesPath, 'backend', bin);
  }
  return path.join(__dirname, '..', 'build', bin);
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1280,
    height: 820,
    minWidth: 960,
    minHeight: 640,
    title: 'MixAgent',
    backgroundColor: '#0f1117',
    webPreferences: {
      preload: path.join(__dirname, 'preload.js'),
      contextIsolation: true,
      nodeIntegration: false
    }
  });

  mainWindow.loadFile(path.join(__dirname, 'renderer', 'index.html'));
}

app.whenReady().then(async () => {
  // Request microphone permission on macOS (required for PortAudio capture)
  if (process.platform === 'darwin') {
    const granted = await systemPreferences.askForMediaAccess('microphone');
    if (!granted) {
      console.warn('Microphone permission denied — audio capture will not work');
    }
  }
  createWindow();
  initAutoUpdater();
});

app.on('window-all-closed', () => {
  stopBackend();
  if (process.platform !== 'darwin') app.quit();
});

app.on('activate', () => {
  if (BrowserWindow.getAllWindows().length === 0) createWindow();
});

// ── Config file management ──────────────────────────────────────────

ipcMain.handle('config:load', async () => {
  const configPath = path.join(resolveConfigDir(), 'show.json');
  try {
    const raw = fs.readFileSync(configPath, 'utf-8');
    return { ok: true, data: JSON.parse(raw), path: configPath };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('config:save', async (_event, config) => {
  const configPath = path.join(resolveConfigDir(), 'show.json');
  try {
    fs.writeFileSync(configPath, JSON.stringify(config, null, 4) + '\n', 'utf-8');
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('config:loadEnv', async () => {
  const envPath = app.isPackaged
    ? path.join(app.getPath('userData'), '.env')
    : path.join(__dirname, '..', '.env');
  try {
    const raw = fs.readFileSync(envPath, 'utf-8');
    const env = {};
    for (const line of raw.split('\n')) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;
      const eqIdx = trimmed.indexOf('=');
      if (eqIdx > 0) {
        env[trimmed.slice(0, eqIdx).trim()] = trimmed.slice(eqIdx + 1).trim();
      }
    }
    return { ok: true, data: env };
  } catch {
    return { ok: true, data: {} };
  }
});

ipcMain.handle('config:saveEnv', async (_event, envObj) => {
  const envPath = app.isPackaged
    ? path.join(app.getPath('userData'), '.env')
    : path.join(__dirname, '..', '.env');
  try {
    const lines = Object.entries(envObj).map(([k, v]) => `${k}=${v}`);
    fs.writeFileSync(envPath, lines.join('\n') + '\n', 'utf-8');
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('config:listConsoleConfigs', async () => {
  const dir = resolveConfigDir();
  try {
    const files = fs.readdirSync(dir).filter(f => f.endsWith('.json'));
    return { ok: true, files };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('config:loadFile', async (_event, filename) => {
  const configPath = path.join(resolveConfigDir(), filename);
  try {
    const raw = fs.readFileSync(configPath, 'utf-8');
    return { ok: true, data: JSON.parse(raw) };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

// ── Backend process management ──────────────────────────────────────

function stopBackend() {
  if (backendProcess) {
    backendProcess.kill('SIGTERM');
    backendProcess = null;
  }
}

ipcMain.handle('backend:start', async () => {
  if (backendProcess) {
    return { ok: false, error: 'Backend already running' };
  }
  const binPath = resolveBackendPath();
  if (!fs.existsSync(binPath)) {
    return { ok: false, error: `Backend binary not found at ${binPath}. Run "npm run build-backend" first.` };
  }

  // Load .env and merge into backend process environment
  const envPath = app.isPackaged
    ? path.join(app.getPath('userData'), '.env')
    : path.join(__dirname, '..', '.env');
  const extraEnv = {};
  try {
    const raw = fs.readFileSync(envPath, 'utf-8');
    for (const line of raw.split('\n')) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;
      const eqIdx = trimmed.indexOf('=');
      if (eqIdx > 0) {
        extraEnv[trimmed.slice(0, eqIdx).trim()] = trimmed.slice(eqIdx + 1).trim();
      }
    }
  } catch { /* no .env file, that's fine */ }

  const configDir = resolveConfigDir();
  backendProcess = spawn(binPath, ['--headless', '--config', path.join(configDir, 'show.json')], {
    cwd: app.isPackaged ? process.resourcesPath : path.join(__dirname, '..'),
    env: { ...process.env, ...extraEnv },
    stdio: ['pipe', 'pipe', 'pipe']
  });

  let stdoutBuffer = '';
  backendProcess.stdout.on('data', (data) => {
    stdoutBuffer += data.toString();
    const lines = stdoutBuffer.split('\n');
    // Keep the last (possibly incomplete) chunk in the buffer
    stdoutBuffer = lines.pop() || '';
    for (const line of lines) {
      if (!line) continue;
      try {
        const parsed = JSON.parse(line);
        if (parsed.type === 'approval_request') {
          mainWindow?.webContents.send('approval:new', parsed);
        } else if (parsed.type === 'meter_update') {
          mainWindow?.webContents.send('meters:update', parsed);
        } else if (parsed.type === 'llm_response') {
          mainWindow?.webContents.send('chat:llmResponse', parsed);
        } else if (parsed.type === 'action_executed') {
          mainWindow?.webContents.send('approval:executed', parsed);
        } else {
          mainWindow?.webContents.send('backend:log', { text: line });
        }
      } catch {
        mainWindow?.webContents.send('backend:log', { text: line });
      }
    }
  });

  backendProcess.stderr.on('data', (data) => {
    mainWindow?.webContents.send('backend:log', { text: data.toString(), level: 'error' });
  });

  backendProcess.on('close', (code) => {
    mainWindow?.webContents.send('backend:stopped', { code });
    backendProcess = null;
  });

  return { ok: true };
});

ipcMain.handle('backend:stop', async () => {
  stopBackend();
  return { ok: true };
});

ipcMain.handle('backend:status', async () => {
  return { running: backendProcess !== null };
});

// ── Approval actions (sent to backend via stdin) ────────────────────

ipcMain.handle('approval:approve', async (_event, actionId) => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'approve', id: actionId }) + '\n');
  return { ok: true };
});

ipcMain.handle('approval:reject', async (_event, actionId) => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'reject', id: actionId }) + '\n');
  return { ok: true };
});

ipcMain.handle('approval:approveAll', async () => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'approve_all' }) + '\n');
  return { ok: true };
});

ipcMain.handle('approval:rejectAll', async () => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'reject_all' }) + '\n');
  return { ok: true };
});

// ── Chat (engineer instructions to LLM) ─────────────────────────────

ipcMain.handle('chat:send', async (_event, message) => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'chat', message }) + '\n');
  return { ok: true };
});

// ── Ollama helpers ──────────────────────────────────────────────────

ipcMain.handle('ollama:testConnection', async (_event, host) => {
  const http = host.startsWith('https') ? require('https') : require('http');
  return new Promise((resolve) => {
    const url = new URL('/api/tags', host);
    const req = http.get(url, { timeout: 4000 }, (res) => {
      let body = '';
      res.on('data', (chunk) => { body += chunk; });
      res.on('end', () => {
        try {
          const data = JSON.parse(body);
          const models = (data.models || []).map(m => m.name);
          resolve({ ok: true, models });
        } catch {
          resolve({ ok: false, error: 'Invalid response from Ollama' });
        }
      });
    });
    req.on('error', (err) => {
      resolve({ ok: false, error: err.message });
    });
    req.on('timeout', () => {
      req.destroy();
      resolve({ ok: false, error: 'Connection timed out' });
    });
  });
});

// ── Console connection test ──────────────────────────────────────────

ipcMain.handle('console:testConnection', async (_event, { ip, port, consoleType }) => {
  const net = require('net');
  const dgram = require('dgram');

  if (!ip) return { ok: false, error: 'No IP address provided' };

  // Avantis uses TCP
  if (consoleType === 'avantis') {
    return new Promise((resolve) => {
      const socket = new net.Socket();
      const timer = setTimeout(() => {
        socket.destroy();
        resolve({ ok: false, error: 'Connection timed out — console may be off or unreachable' });
      }, 3000);

      socket.connect(port || 51325, ip, () => {
        clearTimeout(timer);
        socket.destroy();
        resolve({ ok: true, protocol: 'tcp' });
      });

      socket.on('error', (err) => {
        clearTimeout(timer);
        socket.destroy();
        resolve({ ok: false, error: err.message });
      });
    });
  }

  // X32 and Wing use OSC over UDP — send a probe and wait for response
  return new Promise((resolve) => {
    const socket = dgram.createSocket('udp4');
    const timer = setTimeout(() => {
      socket.close();
      resolve({ ok: false, error: 'No response — console may be off or unreachable' });
    }, 3000);

    // OSC message: /xinfo for X32, / for Wing (null-padded to 4-byte boundary + empty type tag)
    const oscAddr = consoleType === 'wing'
      ? Buffer.from('/\0\0\0,\0\0\0')
      : Buffer.from('/xinfo\0\0,\0\0\0');
    const targetPort = port || (consoleType === 'wing' ? 2222 : 10023);

    socket.on('message', () => {
      clearTimeout(timer);
      socket.close();
      resolve({ ok: true, protocol: 'udp' });
    });

    socket.on('error', (err) => {
      clearTimeout(timer);
      socket.close();
      resolve({ ok: false, error: err.message });
    });

    socket.send(oscAddr, targetPort, ip);
  });
});

// ── Audio device detection ──────────────────────────────────────────

ipcMain.handle('audio:testDevices', async () => {
  const { execFile } = require('child_process');

  return new Promise((resolve) => {
    if (process.platform === 'darwin') {
      execFile('system_profiler', ['SPAudioDataType', '-json'], { timeout: 5000 }, (err, stdout) => {
        if (err) return resolve({ ok: false, error: err.message });
        try {
          const data = JSON.parse(stdout);
          const items = data.SPAudioDataType || [];
          const devices = items.map((d, i) => ({
            id: i,
            name: d._name,
            inputs: d.coreaudio_device_input || 0,
            outputs: d.coreaudio_device_output || 0
          }));
          resolve({ ok: true, devices });
        } catch {
          resolve({ ok: false, error: 'Failed to parse audio device list' });
        }
      });
    } else if (process.platform === 'win32') {
      execFile('powershell', ['-Command',
        'Get-CimInstance Win32_SoundDevice | Select-Object Name,Status | ConvertTo-Json'
      ], { timeout: 5000 }, (err, stdout) => {
        if (err) return resolve({ ok: false, error: err.message });
        try {
          let data = JSON.parse(stdout);
          if (!Array.isArray(data)) data = [data];
          const devices = data.map(d => ({ name: d.Name, status: d.Status }));
          resolve({ ok: true, devices });
        } catch {
          resolve({ ok: false, error: 'Failed to parse audio device list' });
        }
      });
    } else {
      // Linux
      execFile('aplay', ['-l'], { timeout: 5000 }, (err, stdout) => {
        if (err) return resolve({ ok: false, error: err.message });
        const devices = [];
        for (const line of stdout.split('\n')) {
          const match = line.match(/^card \d+: .+\[(.+?)\]/);
          if (match) devices.push({ name: match[1].trim() });
        }
        resolve({ ok: true, devices });
      });
    }
  });
});

// ── Demo mode ───────────────────────────────────────────────────────

let demoTimers = [];
let demoRunning = false;

const DEMO_CHANNELS = [
  { index: 1, name: 'Kick', role: 'kick' },
  { index: 2, name: 'Snare', role: 'snare' },
  { index: 3, name: 'Hi-Hat', role: 'hihat' },
  { index: 4, name: 'Tom 1', role: 'tom' },
  { index: 5, name: 'OH L', role: 'overhead' },
  { index: 6, name: 'OH R', role: 'overhead' },
  { index: 7, name: 'Bass DI', role: 'bass' },
  { index: 8, name: 'Gtr L', role: 'guitar' },
  { index: 9, name: 'Gtr R', role: 'guitar' },
  { index: 10, name: 'Keys L', role: 'keys' },
  { index: 11, name: 'Keys R', role: 'keys' },
  { index: 12, name: 'Pad', role: 'synth' },
  { index: 13, name: 'Lead Vox', role: 'vocal' },
  { index: 14, name: 'BG Vox 1', role: 'vocal' },
  { index: 15, name: 'BG Vox 2', role: 'vocal' },
  { index: 16, name: 'Ac Guitar', role: 'guitar' },
];

const DEMO_ACTIONS = [
  { desc: 'Boost Lead Vox fader +2dB', reason: 'Vocals sitting below instruments in the mix', urgency: 1 },
  { desc: 'Cut 400Hz on Gtr L by -2.5dB', reason: 'Guitar masking vocal presence range', urgency: 2 },
  { desc: 'Engage HPF on Bass DI at 40Hz', reason: 'Sub rumble detected below fundamental', urgency: 2 },
  { desc: 'Reduce Kick fader -1.5dB', reason: 'Kick overpowering snare in low-mid range', urgency: 2 },
  { desc: 'Add +1.5dB at 3kHz on Lead Vox', reason: 'Improve vocal clarity and intelligibility', urgency: 2 },
  { desc: 'Pan BG Vox 1 to -30, BG Vox 2 to +30', reason: 'Create stereo width in backing vocals', urgency: 3 },
  { desc: 'Lower OH L/R faders -3dB', reason: 'Overheads bleeding into vocal mics causing cymbal wash', urgency: 1 },
  { desc: 'Increase Keys compression ratio to 3:1', reason: 'Dynamic range too wide for sustained pad sound', urgency: 3 },
  { desc: 'Cut 2.5kHz on Snare by -2dB', reason: 'Harsh ring detected on snare hits', urgency: 1 },
  { desc: 'Boost Ac Guitar at 5kHz +1.5dB', reason: 'Acoustic guitar lacks sparkle in mix', urgency: 3 },
];

const DEMO_LLM_MESSAGES = [
  'Mix analysis: Vocals are sitting well now. Bass and kick have good separation. Monitoring overhead bleed.',
  'Noticed the keys pad is competing with guitars in the 800Hz-1.2kHz range. Suggesting a gentle cut on keys.',
  'Show type detected: Worship band (confidence 0.92). Applying warm vocal-forward mix approach.',
  'Current headroom: +4dB on master bus. All channels within safe operating range.',
  'Feedback risk detected at 2.8kHz — likely from monitor wedge interaction with Lead Vox mic.',
  'Stereo image looks good. Drums centered, guitars panned, vocals prominent. Pads providing nice width.',
];

function startDemo() {
  if (demoRunning) return { ok: false, error: 'Demo already running' };
  demoRunning = true;

  let elapsed = 0;
  let actionIdx = 0;
  const channelState = DEMO_CHANNELS.map(ch => ({
    ...ch,
    rms_db: -30 + Math.random() * 15,
    peak_db: -20 + Math.random() * 10,
  }));

  // Meter updates at 100ms
  const meterTimer = setInterval(() => {
    // Simulate realistic meter movement
    for (const ch of channelState) {
      const target = -35 + Math.random() * 25;
      ch.rms_db += (target - ch.rms_db) * 0.3;
      ch.peak_db = Math.max(ch.rms_db + 2 + Math.random() * 4, ch.peak_db - 0.5);
      // Occasional transient spikes on drums
      if (['kick', 'snare', 'hihat', 'tom'].includes(ch.role) && Math.random() < 0.15) {
        ch.rms_db = -10 + Math.random() * 8;
        ch.peak_db = ch.rms_db + 2;
      }
    }

    // Audio input channels (stereo)
    const audioChannels = [
      { index: 0, name: 'L', rms_db: -25 + Math.random() * 15, peak_db: -15 + Math.random() * 10 },
      { index: 1, name: 'R', rms_db: -25 + Math.random() * 15, peak_db: -15 + Math.random() * 10 },
    ];

    mainWindow?.webContents.send('meters:update', {
      console_channels: channelState.map(ch => ({
        index: ch.index, name: ch.name, rms_db: Math.round(ch.rms_db * 10) / 10, peak_db: Math.round(ch.peak_db * 10) / 10,
      })),
      audio_channels: audioChannels,
    });
  }, 100);
  demoTimers.push(meterTimer);

  // Approval requests every 5-8 seconds
  const approvalTimer = setInterval(() => {
    if (actionIdx >= DEMO_ACTIONS.length) return;
    const action = DEMO_ACTIONS[actionIdx++];
    mainWindow?.webContents.send('approval:new', {
      type: 'approval_request',
      id: `demo-${Date.now()}-${actionIdx}`,
      action: action.desc,
      description: action.desc,
      reason: action.reason,
      urgency: action.urgency,
    });
  }, 5000 + Math.random() * 3000);
  demoTimers.push(approvalTimer);

  // LLM chat responses every 8-12 seconds
  let msgIdx = 0;
  const chatTimer = setInterval(() => {
    if (msgIdx >= DEMO_LLM_MESSAGES.length) return;
    mainWindow?.webContents.send('chat:llmResponse', {
      type: 'llm_response',
      message: DEMO_LLM_MESSAGES[msgIdx++],
    });
  }, 8000 + Math.random() * 4000);
  demoTimers.push(chatTimer);

  // Log messages
  const logMessages = [
    'Agent running — DSP@50ms LLM@5000ms Audio:on',
    'Console: X32 at 192.168.1.100:10023 (demo)',
    'LLM mode: Anthropic-primary (claude-sonnet-4-20250514)',
    'Genre preset: worship — Big pads, clear vocals, emotional dynamics',
    'Channel discovery complete: 16 channels mapped',
  ];
  for (const msg of logMessages) {
    mainWindow?.webContents.send('backend:log', { text: `[demo] ${msg}` });
  }

  // Auto-stop after 60 seconds
  const stopTimer = setTimeout(() => { stopDemo(); }, 60000);
  demoTimers.push(stopTimer);

  // Track elapsed time for status
  const elapsedTimer = setInterval(() => {
    elapsed++;
    mainWindow?.webContents.send('demo:tick', { elapsed, remaining: 60 - elapsed });
  }, 1000);
  demoTimers.push(elapsedTimer);

  return { ok: true };
}

function stopDemo() {
  for (const t of demoTimers) { clearInterval(t); clearTimeout(t); }
  demoTimers = [];
  demoRunning = false;
  mainWindow?.webContents.send('demo:stopped');
}

ipcMain.handle('demo:start', async () => startDemo());
ipcMain.handle('demo:stop', async () => { stopDemo(); return { ok: true }; });

// ── Approval mode change ────────────────────────────────────────────

ipcMain.handle('approval:setMode', async (_event, mode) => {
  if (!backendProcess) return { ok: false, error: 'Backend not running' };
  backendProcess.stdin.write(JSON.stringify({ command: 'set_approval_mode', mode }) + '\n');
  return { ok: true };
});

// ── Auto-updater ────────────────────────────────────────────────────

function initAutoUpdater() {
  if (!app.isPackaged) return; // skip in dev

  autoUpdater.autoDownload = false;
  autoUpdater.autoInstallOnAppQuit = true;

  autoUpdater.on('update-available', (info) => {
    mainWindow?.webContents.send('updater:available', {
      version: info.version,
      releaseNotes: info.releaseNotes || '',
      releaseDate: info.releaseDate || ''
    });
  });

  autoUpdater.on('update-not-available', () => {
    mainWindow?.webContents.send('updater:upToDate');
  });

  autoUpdater.on('download-progress', (progress) => {
    mainWindow?.webContents.send('updater:progress', {
      percent: Math.round(progress.percent),
      transferred: progress.transferred,
      total: progress.total
    });
  });

  autoUpdater.on('update-downloaded', () => {
    mainWindow?.webContents.send('updater:ready');
  });

  autoUpdater.on('error', (err) => {
    mainWindow?.webContents.send('updater:error', { error: err.message });
  });

  // Check on launch, then every 30 minutes
  autoUpdater.checkForUpdates().catch(() => {});
  setInterval(() => {
    autoUpdater.checkForUpdates().catch(() => {});
  }, 30 * 60 * 1000);
}

ipcMain.handle('updater:check', async () => {
  try {
    const result = await autoUpdater.checkForUpdates();
    return { ok: true, version: result?.updateInfo?.version };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('updater:download', async () => {
  try {
    await autoUpdater.downloadUpdate();
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err.message };
  }
});

ipcMain.handle('updater:install', async () => {
  stopBackend();
  autoUpdater.quitAndInstall();
});
