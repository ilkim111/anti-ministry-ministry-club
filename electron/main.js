const { app, BrowserWindow, ipcMain, dialog } = require('electron');
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

app.whenReady().then(() => {
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

  backendProcess.stdout.on('data', (data) => {
    const lines = data.toString().split('\n').filter(Boolean);
    for (const line of lines) {
      // Parse structured log lines from the backend
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
