// ── State ────────────────────────────────────────────────────────────
let currentConfig = {};
let pendingActions = [];
let actionHistory = [];
let isRunning = false;

// ── DOM refs ─────────────────────────────────────────────────────────
const $ = (sel) => document.querySelector(sel);
const $$ = (sel) => document.querySelectorAll(sel);

// ── Tab navigation ───────────────────────────────────────────────────
$$('.tab').forEach(tab => {
  tab.addEventListener('click', () => {
    $$('.tab').forEach(t => t.classList.remove('active'));
    $$('.panel').forEach(p => p.classList.remove('active'));
    tab.classList.add('active');
    $(`#panel-${tab.dataset.panel}`).classList.add('active');
  });
});

// ── Config: field <-> JSON mapping ───────────────────────────────────
const CONFIG_FIELDS = [
  'console_type', 'console_ip', 'console_port',
  'dsp_interval_ms', 'llm_interval_ms', 'meter_refresh_ms',
  'approval_mode', 'headless',
  'anthropic_model', 'openai_model',
  'llm_temperature', 'llm_max_tokens',
  'ollama_primary', 'ollama_host', 'ollama_model',
  'audio_channels', 'audio_device_id', 'audio_sample_rate', 'audio_fft_size',
  'genre', 'preferences_file'
];

const ENV_FIELDS = [
  'ANTHROPIC_API_KEY', 'OPENAI_API_KEY', 'MIXAGENT_LOG_LEVEL'
];

function populateConfig(data) {
  currentConfig = data;
  for (const key of CONFIG_FIELDS) {
    const el = $(`#cfg-${key}`);
    if (!el) continue;
    if (el.type === 'checkbox') {
      el.checked = !!data[key];
    } else if (key === 'ollama_model') {
      // Store value; will be selected after model list loads
      el.dataset.pending = data[key] ?? '';
      setOllamaModelValue(data[key] ?? '');
    } else {
      el.value = data[key] ?? '';
    }
  }
  updateOllamaPrimaryUI();
}

function setOllamaModelValue(modelName) {
  const sel = $('#cfg-ollama_model');
  // If the model is already in the list, select it
  for (const opt of sel.options) {
    if (opt.value === modelName) { sel.value = modelName; return; }
  }
  // Otherwise add it as a placeholder option
  if (modelName) {
    const opt = document.createElement('option');
    opt.value = modelName;
    opt.textContent = modelName;
    sel.appendChild(opt);
    sel.value = modelName;
  }
}

function updateOllamaPrimaryUI() {
  const enabled = $('#cfg-ollama_primary').checked;
  const statusEl = $('#ollama-status');
  if (enabled) {
    statusEl.textContent = 'primary';
    statusEl.className = 'conn-status conn-primary';
  } else {
    statusEl.textContent = 'fallback';
    statusEl.className = 'conn-status conn-fallback';
  }
}

function gatherConfig() {
  const cfg = { ...currentConfig };
  for (const key of CONFIG_FIELDS) {
    const el = $(`#cfg-${key}`);
    if (!el) continue;
    if (el.type === 'checkbox') {
      cfg[key] = el.checked;
    } else if (el.type === 'number') {
      cfg[key] = el.value === '' ? 0 : Number(el.value);
    } else {
      cfg[key] = el.value;
    }
  }
  return cfg;
}

function populateEnv(data) {
  for (const key of ENV_FIELDS) {
    const el = $(`#env-${key}`);
    if (el) el.value = data[key] ?? '';
  }
}

function gatherEnv() {
  const env = {};
  for (const key of ENV_FIELDS) {
    const el = $(`#env-${key}`);
    if (el && el.value) env[key] = el.value;
  }
  return env;
}

// ── Config: Load on startup ──────────────────────────────────────────
async function loadConfig() {
  const [configRes, envRes, listRes] = await Promise.all([
    window.mixagent.config.load(),
    window.mixagent.config.loadEnv(),
    window.mixagent.config.listConfigs()
  ]);

  if (configRes.ok) populateConfig(configRes.data);
  if (envRes.ok) populateEnv(envRes.data);

  if (listRes.ok) {
    const sel = $('#config-file-select');
    sel.innerHTML = '';
    for (const f of listRes.files) {
      const opt = document.createElement('option');
      opt.value = f;
      opt.textContent = f;
      if (f === 'show.json') opt.selected = true;
      sel.appendChild(opt);
    }
  }
}

$('#config-file-select').addEventListener('change', async (e) => {
  const res = await window.mixagent.config.loadFile(e.target.value);
  if (res.ok) populateConfig(res.data);
});

$('#btn-config-save').addEventListener('click', async () => {
  const cfg = gatherConfig();
  const res = await window.mixagent.config.save(cfg);
  if (res.ok) {
    showToast('Config saved');
  } else {
    showToast('Save failed: ' + res.error, 'error');
  }
});

$('#btn-env-save').addEventListener('click', async () => {
  const env = gatherEnv();
  const res = await window.mixagent.config.saveEnv(env);
  if (res.ok) {
    showToast('.env saved');
  } else {
    showToast('Save failed: ' + res.error, 'error');
  }
});

// ── Ollama: test connection & model list ─────────────────────────────
async function testOllamaConnection() {
  const host = $('#cfg-ollama_host').value.trim() || 'http://localhost:11434';
  const statusEl = $('#ollama-status');
  const infoEl = $('#ollama-model-info');
  const btn = $('#btn-ollama-test');

  btn.disabled = true;
  btn.textContent = '...';
  statusEl.textContent = 'connecting...';
  statusEl.className = 'conn-status conn-testing';

  const res = await window.mixagent.ollama.testConnection(host);

  btn.disabled = false;
  btn.textContent = 'Test';

  if (res.ok) {
    populateOllamaModels(res.models);
    statusEl.textContent = `connected (${res.models.length} models)`;
    statusEl.className = 'conn-status conn-ok';
    infoEl.textContent = '';
    showToast(`Ollama connected — ${res.models.length} model(s) available`);
  } else {
    statusEl.textContent = 'offline';
    statusEl.className = 'conn-status conn-fail';
    infoEl.textContent = `Could not reach Ollama at ${host}: ${res.error}`;
    showToast('Ollama connection failed: ' + res.error, 'error');
  }
}

function populateOllamaModels(models) {
  const sel = $('#cfg-ollama_model');
  const current = sel.value || sel.dataset.pending || '';
  sel.innerHTML = '';

  if (models.length === 0) {
    const opt = document.createElement('option');
    opt.value = '';
    opt.textContent = '-- no models found --';
    sel.appendChild(opt);
    return;
  }

  for (const name of models) {
    const opt = document.createElement('option');
    opt.value = name;
    opt.textContent = name;
    sel.appendChild(opt);
  }

  // Restore previous selection if it exists in the list
  if (current && models.includes(current)) {
    sel.value = current;
  } else {
    sel.value = models[0];
  }
}

$('#btn-ollama-test').addEventListener('click', testOllamaConnection);
$('#btn-ollama-refresh').addEventListener('click', testOllamaConnection);

$('#cfg-ollama_primary').addEventListener('change', () => {
  updateOllamaPrimaryUI();
});

// ── Console: test connection ─────────────────────────────────────────
async function testConsoleConnection() {
  const ip = $('#cfg-console_ip').value.trim();
  const port = parseInt($('#cfg-console_port').value) || 0;
  const consoleType = $('#cfg-console_type').value;
  const statusEl = $('#console-conn-status');
  const infoEl = $('#console-conn-info');
  const btn = $('#btn-console-test');

  if (!ip) {
    showToast('Enter a console IP address first', 'error');
    return;
  }

  btn.disabled = true;
  btn.textContent = '...';
  statusEl.textContent = 'connecting...';
  statusEl.className = 'conn-status conn-testing';
  infoEl.textContent = '';

  const res = await window.mixagent.consoleTest.testConnection({ ip, port, consoleType });

  btn.disabled = false;
  btn.textContent = 'Test';

  if (res.ok) {
    statusEl.textContent = `reachable (${res.protocol.toUpperCase()})`;
    statusEl.className = 'conn-status conn-ok';
    infoEl.textContent = '';
    showToast(`Console reachable at ${ip} via ${res.protocol.toUpperCase()}`);
  } else {
    statusEl.textContent = 'unreachable';
    statusEl.className = 'conn-status conn-fail';
    infoEl.textContent = res.error;
    showToast('Console connection failed', 'error');
  }
}

$('#btn-console-test').addEventListener('click', testConsoleConnection);

// ── Audio: detect devices ───────────────────────────────────────────
async function testAudioDevices() {
  const statusEl = $('#audio-conn-status');
  const listEl = $('#audio-device-list');
  const btn = $('#btn-audio-test');

  btn.disabled = true;
  btn.textContent = '...';
  statusEl.textContent = 'scanning...';
  statusEl.className = 'conn-status conn-testing';

  const res = await window.mixagent.audio.testDevices();

  btn.disabled = false;
  btn.textContent = 'Detect Devices';

  if (res.ok && res.devices.length > 0) {
    statusEl.textContent = `${res.devices.length} device(s)`;
    statusEl.className = 'conn-status conn-ok';
    listEl.textContent = '';
    populateAudioDevices(res.devices);
    showToast(`Found ${res.devices.length} audio device(s)`);
  } else if (res.ok) {
    statusEl.textContent = 'no devices';
    statusEl.className = 'conn-status conn-fail';
    listEl.textContent = 'No audio input devices detected. Check USB/Dante connections.';
    showToast('No audio devices found', 'error');
  } else {
    statusEl.textContent = 'error';
    statusEl.className = 'conn-status conn-fail';
    listEl.textContent = res.error;
    showToast('Audio detection failed', 'error');
  }
}

function populateAudioDevices(devices) {
  const sel = $('#cfg-audio_device_id');
  const current = sel.value || '-1';

  // Clear existing options safely
  while (sel.firstChild) sel.removeChild(sel.firstChild);

  // Default option
  const defOpt = document.createElement('option');
  defOpt.value = '-1';
  defOpt.textContent = 'Default (system)';
  sel.appendChild(defOpt);

  for (const d of devices) {
    const opt = document.createElement('option');
    opt.value = String(d.id);
    opt.textContent = d.name;
    sel.appendChild(opt);
  }

  // Restore previous selection if it exists
  if (current !== '-1') {
    for (const opt of sel.options) {
      if (opt.value === current) { sel.value = current; return; }
    }
  }
}

$('#btn-audio-test').addEventListener('click', testAudioDevices);

// ── Backend start/stop ───────────────────────────────────────────────
function setRunning(running) {
  isRunning = running;
  const badge = $('#backend-status');
  badge.textContent = running ? 'Running' : 'Offline';
  badge.className = 'status-badge ' + (running ? 'running' : 'offline');
  $('#btn-start').disabled = running;
  $('#btn-stop').disabled = !running;
}

$('#btn-start').addEventListener('click', async () => {
  const res = await window.mixagent.backend.start();
  if (res.ok) {
    setRunning(true);
    appendLog('Engine started');
  } else {
    showToast(res.error, 'error');
  }
});

$('#btn-stop').addEventListener('click', async () => {
  const res = await window.mixagent.backend.stop();
  if (res.ok) {
    setRunning(false);
    appendLog('Engine stopped');
  }
});

window.mixagent.backend.onStopped((data) => {
  setRunning(false);
  appendLog(`Engine exited with code ${data.code}`);
});

// ── Logs ─────────────────────────────────────────────────────────────
function appendLog(text, level) {
  const logEl = $('#log-output');
  const line = document.createElement('span');
  if (level === 'error') line.className = 'log-error';
  const ts = new Date().toLocaleTimeString();
  line.textContent = `[${ts}] ${text}\n`;
  logEl.appendChild(line);
  logEl.scrollTop = logEl.scrollHeight;
}

window.mixagent.backend.onLog((data) => {
  appendLog(data.text, data.level);
});

$('#btn-clear-logs').addEventListener('click', () => {
  $('#log-output').innerHTML = '';
});

// ── Approval Queue ───────────────────────────────────────────────────
const URGENCY_LABELS = ['immediate', 'fast', 'normal', 'low'];

function renderApprovalList() {
  const container = $('#approval-list');
  $('#approval-count').textContent = `${pendingActions.length} pending`;

  if (pendingActions.length === 0) {
    container.innerHTML = '<div class="empty-state">No pending actions.</div>';
    return;
  }

  container.innerHTML = '';
  pendingActions.forEach((item, idx) => {
    const el = document.createElement('div');
    el.className = 'approval-item';

    const urgencyLevel = URGENCY_LABELS[item.urgency ?? 2];

    el.innerHTML = `
      <span class="urgency ${urgencyLevel}">${urgencyLevel}</span>
      <div class="desc">
        <div>${escapeHtml(item.description || item.action || 'Unknown action')}</div>
        ${item.reason ? `<div class="reason">${escapeHtml(item.reason)}</div>` : ''}
      </div>
      <div class="actions">
        <button class="btn btn-success btn-sm btn-approve" data-idx="${idx}">Approve</button>
        <button class="btn btn-danger btn-sm btn-reject" data-idx="${idx}">Reject</button>
      </div>
    `;
    container.appendChild(el);
  });

  container.querySelectorAll('.btn-approve').forEach(btn => {
    btn.addEventListener('click', async () => {
      const idx = parseInt(btn.dataset.idx);
      const item = pendingActions[idx];
      await window.mixagent.approval.approve(item.id);
      addHistory(item, 'approved');
      pendingActions.splice(idx, 1);
      renderApprovalList();
    });
  });

  container.querySelectorAll('.btn-reject').forEach(btn => {
    btn.addEventListener('click', async () => {
      const idx = parseInt(btn.dataset.idx);
      const item = pendingActions[idx];
      await window.mixagent.approval.reject(item.id);
      addHistory(item, 'rejected');
      pendingActions.splice(idx, 1);
      renderApprovalList();
    });
  });
}

window.mixagent.approval.onNew((data) => {
  pendingActions.push(data);
  renderApprovalList();
  // Flash the tab if not active
  const tab = document.querySelector('[data-panel="approval"]');
  if (!tab.classList.contains('active')) {
    tab.style.color = 'var(--orange)';
    setTimeout(() => { tab.style.color = ''; }, 2000);
  }
});

window.mixagent.approval.onExecuted((data) => {
  addHistory(data, 'executed');
});

$('#btn-approve-all').addEventListener('click', async () => {
  await window.mixagent.approval.approveAll();
  for (const item of pendingActions) addHistory(item, 'approved');
  pendingActions = [];
  renderApprovalList();
});

$('#btn-reject-all').addEventListener('click', async () => {
  await window.mixagent.approval.rejectAll();
  for (const item of pendingActions) addHistory(item, 'rejected');
  pendingActions = [];
  renderApprovalList();
});

$('#approval-mode-live').addEventListener('change', async (e) => {
  await window.mixagent.approval.setMode(e.target.value);
  showToast(`Approval mode: ${e.target.value}`);
});

function addHistory(item, status) {
  const ts = new Date().toLocaleTimeString();
  const statusIcons = { approved: '+', rejected: 'x', executed: '>' };
  actionHistory.unshift({ ...item, status, ts, icon: statusIcons[status] || '?' });
  if (actionHistory.length > 100) actionHistory.pop();
  renderHistory();
}

function renderHistory() {
  const container = $('#action-history');
  if (actionHistory.length === 0) {
    container.innerHTML = '<div class="empty-state">No actions yet.</div>';
    return;
  }
  container.innerHTML = '';
  for (const item of actionHistory.slice(0, 50)) {
    const el = document.createElement('div');
    el.className = `history-item ${item.status}`;
    el.innerHTML = `
      <span class="timestamp">${item.ts}</span>
      <span class="status-icon">[${item.icon}]</span>
      <span>${escapeHtml(item.description || item.action || '')}</span>
    `;
    container.appendChild(el);
  }
}

$('#btn-clear-history').addEventListener('click', () => {
  actionHistory = [];
  renderHistory();
});

// ── Chat ─────────────────────────────────────────────────────────────
$('#chat-form').addEventListener('submit', async (e) => {
  e.preventDefault();
  const input = $('#chat-input');
  const msg = input.value.trim();
  if (!msg) return;

  appendChatMessage(msg, 'user');
  input.value = '';

  const res = await window.mixagent.chat.send(msg);
  if (!res.ok) {
    appendChatMessage('Error: ' + res.error, 'system');
  }
});

window.mixagent.chat.onResponse((data) => {
  appendChatMessage(data.message || data.text || JSON.stringify(data), 'assistant');
});

function appendChatMessage(text, role) {
  const container = $('#chat-messages');
  const el = document.createElement('div');
  el.className = `chat-msg ${role}`;

  if (role === 'user') {
    el.innerHTML = `<strong>You:</strong> ${escapeHtml(text)}`;
  } else if (role === 'assistant') {
    el.innerHTML = `<strong>Agent:</strong> ${escapeHtml(text)}`;
  } else {
    el.innerHTML = `<strong>System:</strong> ${escapeHtml(text)}`;
  }

  container.appendChild(el);
  container.scrollTop = container.scrollHeight;
}

// ── Toast notifications ──────────────────────────────────────────────
function showToast(msg, type = 'info') {
  let toast = document.getElementById('toast');
  if (!toast) {
    toast = document.createElement('div');
    toast.id = 'toast';
    toast.style.cssText = `
      position: fixed; bottom: 1.5rem; right: 1.5rem;
      padding: 8px 16px; border-radius: 6px;
      font-family: var(--font); font-size: 12px;
      z-index: 999; transition: opacity 0.3s;
      pointer-events: none;
    `;
    document.body.appendChild(toast);
  }
  toast.textContent = msg;
  toast.style.background = type === 'error' ? 'var(--red-dim)' : 'var(--green-dim)';
  toast.style.color = type === 'error' ? 'var(--red)' : 'var(--green)';
  toast.style.opacity = '1';
  clearTimeout(toast._timer);
  toast._timer = setTimeout(() => { toast.style.opacity = '0'; }, 2500);
}

// ── Utils ────────────────────────────────────────────────────────────
function escapeHtml(str) {
  const div = document.createElement('div');
  div.textContent = str;
  return div.innerHTML;
}

// ── Meters ───────────────────────────────────────────────────────────
function dbToPercent(db) {
  // Map dB to 0-100%: -60dB = 0%, 0dB = 100%
  if (db <= -60) return 0;
  if (db >= 0) return 100;
  return ((db + 60) / 60) * 100;
}

function getMeterClass(db) {
  if (db >= -1) return 'signal-clip';
  if (db >= -6) return 'signal-hot';
  return '';
}

function renderMeterStrip(containerId, channels) {
  const container = document.getElementById(containerId);
  if (!container) return;

  // Build meter DOM on first call or channel count change
  if (container.dataset.count !== String(channels.length)) {
    // Clear safely
    while (container.firstChild) container.removeChild(container.firstChild);
    container.dataset.count = String(channels.length);

    for (const ch of channels) {
      const col = document.createElement('div');
      col.className = 'meter-channel';
      col.dataset.idx = ch.index;

      const dbLabel = document.createElement('span');
      dbLabel.className = 'meter-db';
      dbLabel.textContent = '--';

      const track = document.createElement('div');
      track.className = 'meter-bar-track';

      const fill = document.createElement('div');
      fill.className = 'meter-bar-fill';
      fill.style.height = '0%';

      const peak = document.createElement('div');
      peak.className = 'meter-peak';
      peak.style.bottom = '0%';

      track.appendChild(fill);
      track.appendChild(peak);

      const label = document.createElement('span');
      label.className = 'meter-label';
      label.textContent = ch.name || ch.index;
      label.title = ch.name || `Ch ${ch.index}`;

      col.appendChild(dbLabel);
      col.appendChild(track);
      col.appendChild(label);
      container.appendChild(col);
    }
    return;
  }

  // Update existing meters
  const cols = container.querySelectorAll('.meter-channel');
  for (let i = 0; i < channels.length && i < cols.length; i++) {
    const ch = channels[i];
    const col = cols[i];
    const fill = col.querySelector('.meter-bar-fill');
    const peak = col.querySelector('.meter-peak');
    const dbLabel = col.querySelector('.meter-db');

    const rmsPercent = dbToPercent(ch.rms_db ?? -60);
    const peakPercent = dbToPercent(ch.peak_db ?? -60);

    fill.style.height = rmsPercent + '%';
    fill.className = 'meter-bar-fill ' + getMeterClass(ch.rms_db ?? -60);
    peak.style.bottom = peakPercent + '%';

    const dbVal = ch.rms_db ?? -60;
    dbLabel.textContent = dbVal <= -60 ? '-inf' : Math.round(dbVal);
  }
}

window.mixagent.meters.onUpdate((data) => {
  $('#meters-status').textContent = 'Live';

  if (data.console_channels) {
    renderMeterStrip('meters-console', data.console_channels);
  }

  if (data.audio_channels) {
    renderMeterStrip('meters-audio', data.audio_channels);
  }
});

// ── Auto-updater UI ──────────────────────────────────────────────────
function showUpdateBanner(msg, { download = false, install = false } = {}) {
  const banner = $('#update-banner');
  $('#update-message').textContent = msg;
  banner.style.display = 'flex';
  $('#btn-update-download').style.display = download ? 'inline-block' : 'none';
  $('#btn-update-install').style.display = install ? 'inline-block' : 'none';
}

function hideUpdateBanner() {
  $('#update-banner').style.display = 'none';
}

window.mixagent.updater.onAvailable((data) => {
  showUpdateBanner(`Update available: v${data.version}`, { download: true });
});

window.mixagent.updater.onUpToDate(() => {
  // silent — no banner needed
});

window.mixagent.updater.onProgress((data) => {
  const bar = $('#update-progress-bar');
  const progress = $('#update-progress');
  progress.style.display = 'block';
  bar.style.width = `${data.percent}%`;
  $('#update-message').textContent = `Downloading update... ${data.percent}%`;
});

window.mixagent.updater.onReady(() => {
  $('#update-progress').style.display = 'none';
  showUpdateBanner('Update downloaded. Restart to apply.', { install: true });
});

window.mixagent.updater.onError((data) => {
  showUpdateBanner('Update check failed: ' + data.error);
});

$('#btn-update-download').addEventListener('click', async () => {
  $('#btn-update-download').style.display = 'none';
  const res = await window.mixagent.updater.download();
  if (!res.ok) showToast('Download failed: ' + res.error, 'error');
});

$('#btn-update-install').addEventListener('click', () => {
  window.mixagent.updater.install();
});

$('#btn-update-dismiss').addEventListener('click', hideUpdateBanner);

// ── Demo mode ────────────────────────────────────────────────────────
let demoActive = false;

function setDemoState(active) {
  demoActive = active;
  const btn = $('#btn-demo');
  const badge = $('#backend-status');
  if (active) {
    btn.textContent = 'Stop Demo';
    btn.className = 'btn btn-danger';
    badge.textContent = 'Demo';
    badge.className = 'status-badge running';
    $('#btn-start').disabled = true;
    // Switch to meters tab to show the action
    document.querySelector('[data-panel="meters"]').click();
  } else {
    btn.textContent = 'Demo';
    btn.className = 'btn btn-secondary';
    if (!isRunning) {
      badge.textContent = 'Offline';
      badge.className = 'status-badge offline';
    }
    $('#btn-start').disabled = isRunning;
    $('#meters-status').textContent = 'Waiting for engine...';
  }
}

$('#btn-demo').addEventListener('click', async () => {
  if (demoActive) {
    await window.mixagent.demo.stop();
    setDemoState(false);
    showToast('Demo stopped');
  } else {
    const res = await window.mixagent.demo.start();
    if (res.ok) {
      setDemoState(true);
      showToast('Demo started — 60s of simulated mix data');
    } else {
      showToast(res.error, 'error');
    }
  }
});

window.mixagent.demo.onTick((data) => {
  $('#meters-status').textContent = `Demo: ${data.remaining}s remaining`;
});

window.mixagent.demo.onStopped(() => {
  setDemoState(false);
  showToast('Demo finished');
});

// ── OSC Test Panel ───────────────────────────────────────────

function updateOscParamUI() {
  const param = $('#osc-param').value;
  const bandLabel = $('#osc-band-label');
  const auxLabel = $('#osc-aux-label');
  const customLabel = $('#osc-custom-label');
  const valueLabel = $('#osc-value-label');
  const dbLabel = $('#osc-db-label');
  const hint = $('#osc-value-hint');

  bandLabel.style.display = param.startsWith('eq_') ? '' : 'none';
  auxLabel.style.display = param === 'send' ? '' : 'none';
  customLabel.style.display = param === 'custom' ? '' : 'none';

  const noValue = param === 'mute' || param === 'unmute';
  valueLabel.style.display = noValue ? 'none' : '';

  const hints = {
    fader: 'X32 fader: 0.0 = -inf, 0.5 = -10dB, 0.75 = 0dB, 1.0 = +10dB',
    mute: 'Sends mix/on = 0 (muted)',
    unmute: 'Sends mix/on = 1 (unmuted)',
    pan: '0.0 = full left, 0.5 = center, 1.0 = full right',
    eq_gain: 'Normalized 0.0-1.0 (maps to -15dB to +15dB)',
    eq_freq: 'Normalized 0.0-1.0 (maps to 20Hz-20kHz log scale)',
    eq_q: 'Normalized 0.0-1.0 (maps to 10-0.3 Q)',
    comp_thresh: 'Normalized 0.0-1.0 (maps to -60dB to 0dB)',
    comp_ratio: 'Normalized 0.0-1.0 (maps to 1:1 to inf:1)',
    hpf: 'Normalized 0.0-1.0 (maps to 20Hz-400Hz)',
    send: 'Normalized 0.0-1.0 fader level',
    custom: 'Enter raw OSC address and float value',
  };
  hint.textContent = hints[param] || '';

  const showDb = $('#osc-show-db').checked && param === 'fader';
  dbLabel.style.display = showDb ? '' : 'none';
  if (showDb) valueLabel.style.display = 'none';
}

$('#osc-param').addEventListener('change', updateOscParamUI);
$('#osc-show-db').addEventListener('change', updateOscParamUI);

function dbToFaderFloat(db) {
  if (db <= -90) return 0.0;
  if (db >= 10) return 1.0;
  if (db < -60) return (db + 90) / 30 * 0.0625;
  if (db < -30) return 0.0625 + (db + 60) / 30 * 0.1875;
  if (db < -10) return 0.25 + (db + 30) / 20 * 0.25;
  return 0.5 + (db + 10) / 20 * 0.5;
}

function faderFloatToDb(f) {
  if (f <= 0) return -Infinity;
  if (f >= 1) return 10;
  if (f < 0.0625) return -90 + f / 0.0625 * 30;
  if (f < 0.25) return -60 + (f - 0.0625) / 0.1875 * 30;
  if (f < 0.5) return -30 + (f - 0.25) / 0.25 * 20;
  return -10 + (f - 0.5) / 0.5 * 20;
}

function appendOscLog(text, type) {
  const log = $('#osc-log');
  const line = document.createElement('span');
  if (type === 'error') line.className = 'log-error';
  if (type === 'sent') line.style.color = 'var(--blue, #5b9)';
  if (type === 'received') line.style.color = 'var(--green, #5b9)';
  const ts = new Date().toLocaleTimeString();
  line.textContent = `[${ts}] ${text}\n`;
  log.appendChild(line);
  log.scrollTop = log.scrollHeight;
}

$('#btn-osc-send').addEventListener('click', async () => {
  const channel = parseInt($('#osc-channel').value) || 1;
  const param = $('#osc-param').value;
  const band = parseInt($('#osc-band').value) || 1;
  const aux = parseInt($('#osc-aux').value) || 1;
  const customAddr = param === 'custom' ? $('#osc-custom-addr').value : null;

  let value;
  if ($('#osc-show-db').checked && param === 'fader') {
    const db = parseFloat($('#osc-db-value').value);
    if (isNaN(db)) { showToast('Enter a valid dB value', 'error'); return; }
    value = dbToFaderFloat(db);
    appendOscLog(`Converting ${db} dB -> ${value.toFixed(4)} normalized`, 'sent');
  } else if (param !== 'mute' && param !== 'unmute') {
    value = parseFloat($('#osc-value').value);
    if (isNaN(value)) { showToast('Enter a valid value', 'error'); return; }
  }

  appendOscLog(`SEND ch${channel} ${param} = ${value ?? '(toggle)'}`, 'sent');

  const res = await window.mixagent.oscTest.send({ channel, param, value, band, aux, customAddr });
  if (res.ok) {
    let msg = `OK: ${res.address}`;
    if (res.echo) {
      msg += ` -> echo: ${JSON.stringify(res.echo.values)}`;
    } else if (res.note) {
      msg += ` (${res.note})`;
    }
    appendOscLog(msg, 'received');
  } else {
    appendOscLog(`ERROR: ${res.error}`, 'error');
  }
});

$('#btn-osc-query').addEventListener('click', async () => {
  const channel = parseInt($('#osc-channel').value) || 1;
  const param = $('#osc-param').value;
  const band = parseInt($('#osc-band').value) || 1;
  const aux = parseInt($('#osc-aux').value) || 1;
  const customAddr = param === 'custom' ? $('#osc-custom-addr').value : null;

  appendOscLog(`QUERY ch${channel} ${param}`, 'sent');

  const res = await window.mixagent.oscTest.query({ channel, param, band, aux, customAddr });
  if (res.ok) {
    let msg = `${res.address} = ${JSON.stringify(res.values)}`;
    if (param === 'fader' && res.values.length > 0) {
      const db = faderFloatToDb(res.values[0]);
      msg += ` (${db === -Infinity ? '-inf' : db.toFixed(1)} dB)`;
    }
    appendOscLog(msg, 'received');
  } else {
    appendOscLog(`ERROR: ${res.error}`, 'error');
  }
});

$$('.osc-quick').forEach(btn => {
  btn.addEventListener('click', async () => {
    const channel = parseInt($('#osc-channel').value) || 1;
    const action = btn.dataset.action;

    let opts = { channel };
    switch (action) {
      case 'mute':         opts.param = 'mute'; break;
      case 'unmute':       opts.param = 'unmute'; break;
      case 'fader-unity':  opts.param = 'fader'; opts.value = 0.75; break;
      case 'fader-off':    opts.param = 'fader'; opts.value = 0.0; break;
      case 'fader-minus10':opts.param = 'fader'; opts.value = 0.5; break;
      case 'hpf-80':       opts.param = 'hpf'; opts.value = 0.293; break;
      case 'hpf-120':      opts.param = 'hpf'; opts.value = 0.370; break;
      case 'pan-center':   opts.param = 'pan'; opts.value = 0.5; break;
      case 'query-all': {
        appendOscLog(`--- Query all params for ch${channel} ---`, 'sent');
        for (const p of ['fader', 'mute', 'pan', 'hpf']) {
          const r = await window.mixagent.oscTest.query({ channel, param: p });
          if (r.ok) {
            let msg = `${r.address} = ${JSON.stringify(r.values)}`;
            if (p === 'fader' && r.values.length > 0) {
              const db = faderFloatToDb(r.values[0]);
              msg += ` (${db === -Infinity ? '-inf' : db.toFixed(1)} dB)`;
            }
            if (p === 'mute' && r.values.length > 0) {
              msg += ` (${r.values[0] === 1 ? 'unmuted' : 'MUTED'})`;
            }
            appendOscLog(msg, 'received');
          } else {
            appendOscLog(`${p}: ${r.error}`, 'error');
          }
        }
        for (let b = 1; b <= 4; b++) {
          const r = await window.mixagent.oscTest.query({ channel, param: 'eq_gain', band: b });
          if (r.ok) appendOscLog(`${r.address} = ${JSON.stringify(r.values)}`, 'received');
        }
        return;
      }
    }

    if (action !== 'query-all') {
      appendOscLog(`QUICK: ch${channel} ${action}`, 'sent');
      const res = await window.mixagent.oscTest.send(opts);
      if (res.ok) {
        appendOscLog(`OK: ${res.address}`, 'received');
      } else {
        appendOscLog(`ERROR: ${res.error}`, 'error');
      }
    }
  });
});

$('#btn-osc-clear').addEventListener('click', () => {
  while ($('#osc-log').firstChild) $('#osc-log').removeChild($('#osc-log').firstChild);
});

window.mixagent.oscTest.onResponse((data) => {
  const dir = data.direction === 'sent' ? '->' : '<-';
  appendOscLog(`${dir} ${data.address} [${data.typeTag}] ${JSON.stringify(data.values)} (${data.target || data.source})`, data.direction);
});

updateOscParamUI();

// ── Init ─────────────────────────────────────────────────────────────
(async function init() {
  await loadConfig();

  // Sync approval mode dropdown with config
  const mode = currentConfig.approval_mode || 'auto_urgent';
  $('#approval-mode-live').value = mode;

  // Check if backend is already running
  const status = await window.mixagent.backend.status();
  setRunning(status.running);
})();
