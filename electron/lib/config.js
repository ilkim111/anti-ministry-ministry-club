const path = require('path');
const fs = require('fs');

/**
 * Parse a .env file string into a key-value object.
 */
function parseEnv(raw) {
  const env = {};
  for (const line of raw.split('\n')) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const eqIdx = trimmed.indexOf('=');
    if (eqIdx > 0) {
      env[trimmed.slice(0, eqIdx).trim()] = trimmed.slice(eqIdx + 1).trim();
    }
  }
  return env;
}

/**
 * Serialize a key-value object into .env file format.
 */
function serializeEnv(envObj) {
  return Object.entries(envObj).map(([k, v]) => `${k}=${v}`).join('\n') + '\n';
}

/**
 * Resolve config directory based on packaging state.
 */
function resolveConfigDir(isPackaged, resourcesPath, dirname) {
  if (isPackaged) {
    return path.join(resourcesPath, 'config');
  }
  return path.join(dirname, '..', 'config');
}

/**
 * Resolve backend binary path based on platform and packaging state.
 */
function resolveBackendPath(isPackaged, resourcesPath, dirname, platform) {
  const bin = platform === 'win32' ? 'mixagent.exe' : 'mixagent';
  if (isPackaged) {
    return path.join(resourcesPath, 'backend', bin);
  }
  return path.join(dirname, '..', 'build', bin);
}

/**
 * Load and parse a JSON config file. Returns { ok, data?, error? }.
 */
function loadJsonConfig(configPath) {
  try {
    const raw = fs.readFileSync(configPath, 'utf-8');
    return { ok: true, data: JSON.parse(raw), path: configPath };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

/**
 * Save a JSON config file. Returns { ok, error? }.
 */
function saveJsonConfig(configPath, config) {
  try {
    fs.writeFileSync(configPath, JSON.stringify(config, null, 4) + '\n', 'utf-8');
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

/**
 * List JSON files in a directory. Returns { ok, files?, error? }.
 */
function listJsonFiles(dir) {
  try {
    const files = fs.readdirSync(dir).filter(f => f.endsWith('.json'));
    return { ok: true, files };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

/**
 * Parse a backend stdout line into a typed message.
 * Returns { type, data } where type is the event channel name.
 */
function parseBackendMessage(line) {
  try {
    const parsed = JSON.parse(line);
    const typeMap = {
      'approval_request': 'approval:new',
      'meter_update': 'meters:update',
      'llm_response': 'chat:llmResponse',
      'action_executed': 'approval:executed'
    };
    const channel = typeMap[parsed.type] || 'backend:log';
    if (channel === 'backend:log') {
      return { channel, data: { text: line } };
    }
    return { channel, data: parsed };
  } catch {
    return { channel: 'backend:log', data: { text: line } };
  }
}

module.exports = {
  parseEnv,
  serializeEnv,
  resolveConfigDir,
  resolveBackendPath,
  loadJsonConfig,
  saveJsonConfig,
  listJsonFiles,
  parseBackendMessage
};
