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
  'llm_temperature', 'llm_max_tokens', 'ollama_primary',
  'audio_channels', 'audio_device_id', 'audio_sample_rate', 'audio_fft_size',
  'genre', 'preferences_file'
];

const ENV_FIELDS = [
  'ANTHROPIC_API_KEY', 'MIXAGENT_MODEL', 'OLLAMA_HOST',
  'MIXAGENT_FALLBACK_MODEL', 'MIXAGENT_LOG_LEVEL'
];

function populateConfig(data) {
  currentConfig = data;
  for (const key of CONFIG_FIELDS) {
    const el = $(`#cfg-${key}`);
    if (!el) continue;
    if (el.type === 'checkbox') {
      el.checked = !!data[key];
    } else {
      el.value = data[key] ?? '';
    }
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
