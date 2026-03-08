const fs = require('fs');
const path = require('path');
const os = require('os');
const {
  parseEnv,
  serializeEnv,
  resolveConfigDir,
  resolveBackendPath,
  loadJsonConfig,
  saveJsonConfig,
  listJsonFiles,
  parseBackendMessage
} = require('../lib/config');

// ── parseEnv ──────────────────────────────────────────────────────────

describe('parseEnv', () => {
  test('parses key=value pairs', () => {
    const raw = 'KEY1=value1\nKEY2=value2\n';
    expect(parseEnv(raw)).toEqual({ KEY1: 'value1', KEY2: 'value2' });
  });

  test('skips comments and blank lines', () => {
    const raw = '# this is a comment\n\nKEY=val\n  # another comment\n';
    expect(parseEnv(raw)).toEqual({ KEY: 'val' });
  });

  test('handles values containing equals signs', () => {
    const raw = 'URL=http://host:8080/path?foo=bar\n';
    expect(parseEnv(raw)).toEqual({ URL: 'http://host:8080/path?foo=bar' });
  });

  test('trims whitespace around keys and values', () => {
    const raw = '  KEY  =  value  \n';
    expect(parseEnv(raw)).toEqual({ KEY: 'value' });
  });

  test('returns empty object for empty input', () => {
    expect(parseEnv('')).toEqual({});
  });

  test('ignores lines without equals sign', () => {
    const raw = 'VALID=yes\ninvalid_line\n';
    expect(parseEnv(raw)).toEqual({ VALID: 'yes' });
  });

  test('handles API key format', () => {
    const raw = 'ANTHROPIC_API_KEY=sk-ant-api03-abc123\nMIXAGENT_MODEL=claude-sonnet-4-20250514\n';
    const result = parseEnv(raw);
    expect(result.ANTHROPIC_API_KEY).toBe('sk-ant-api03-abc123');
    expect(result.MIXAGENT_MODEL).toBe('claude-sonnet-4-20250514');
  });
});

// ── serializeEnv ──────────────────────────────────────────────────────

describe('serializeEnv', () => {
  test('serializes object to env format', () => {
    const result = serializeEnv({ KEY1: 'val1', KEY2: 'val2' });
    expect(result).toBe('KEY1=val1\nKEY2=val2\n');
  });

  test('handles empty object', () => {
    expect(serializeEnv({})).toBe('\n');
  });

  test('roundtrips with parseEnv', () => {
    const original = { API_KEY: 'sk-123', MODEL: 'gpt-4' };
    const serialized = serializeEnv(original);
    expect(parseEnv(serialized)).toEqual(original);
  });
});

// ── resolveConfigDir ──────────────────────────────────────────────────

describe('resolveConfigDir', () => {
  test('packaged: uses resourcesPath', () => {
    const result = resolveConfigDir(true, '/app/resources', '/app/electron');
    expect(result).toBe(path.join('/app/resources', 'config'));
  });

  test('dev: uses parent of dirname', () => {
    const result = resolveConfigDir(false, '/ignored', '/home/user/project/electron');
    expect(result).toBe(path.join('/home/user/project', 'config'));
  });
});

// ── resolveBackendPath ────────────────────────────────────────────────

describe('resolveBackendPath', () => {
  test('linux dev: build/mixagent', () => {
    const result = resolveBackendPath(false, '/ignored', '/project/electron', 'linux');
    expect(result).toBe(path.join('/project', 'build', 'mixagent'));
  });

  test('win32 dev: build/mixagent.exe', () => {
    const result = resolveBackendPath(false, '/ignored', '/project/electron', 'win32');
    expect(result).toBe(path.join('/project', 'build', 'mixagent.exe'));
  });

  test('packaged linux: resources/backend/mixagent', () => {
    const result = resolveBackendPath(true, '/app/resources', '/ignored', 'linux');
    expect(result).toBe(path.join('/app/resources', 'backend', 'mixagent'));
  });

  test('packaged win32: resources/backend/mixagent.exe', () => {
    const result = resolveBackendPath(true, '/app/resources', '/ignored', 'win32');
    expect(result).toBe(path.join('/app/resources', 'backend', 'mixagent.exe'));
  });
});

// ── loadJsonConfig / saveJsonConfig ──────────────────────────────────

describe('loadJsonConfig / saveJsonConfig', () => {
  let tmpDir;
  let configPath;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mixagent-test-'));
    configPath = path.join(tmpDir, 'test.json');
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  test('save and load roundtrip', () => {
    const config = { console_type: 'x32', console_ip: '192.168.1.1', port: 10023 };
    const saveResult = saveJsonConfig(configPath, config);
    expect(saveResult).toEqual({ ok: true });

    const loadResult = loadJsonConfig(configPath);
    expect(loadResult.ok).toBe(true);
    expect(loadResult.data).toEqual(config);
    expect(loadResult.path).toBe(configPath);
  });

  test('load returns error for missing file', () => {
    const result = loadJsonConfig('/nonexistent/path.json');
    expect(result.ok).toBe(false);
    expect(result.error).toBeDefined();
  });

  test('load returns error for invalid JSON', () => {
    fs.writeFileSync(configPath, 'not valid json {{{', 'utf-8');
    const result = loadJsonConfig(configPath);
    expect(result.ok).toBe(false);
    expect(result.error).toBeDefined();
  });

  test('save formats with 4-space indent', () => {
    saveJsonConfig(configPath, { key: 'value' });
    const raw = fs.readFileSync(configPath, 'utf-8');
    expect(raw).toContain('    "key"');
    expect(raw.endsWith('\n'));
  });

  test('save returns error for invalid path', () => {
    const result = saveJsonConfig('/nonexistent/dir/file.json', { key: 'val' });
    expect(result.ok).toBe(false);
    expect(result.error).toBeDefined();
  });
});

// ── listJsonFiles ─────────────────────────────────────────────────────

describe('listJsonFiles', () => {
  let tmpDir;

  beforeEach(() => {
    tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'mixagent-test-'));
    fs.writeFileSync(path.join(tmpDir, 'show.json'), '{}');
    fs.writeFileSync(path.join(tmpDir, 'x32.json'), '{}');
    fs.writeFileSync(path.join(tmpDir, 'README.md'), '');
    fs.writeFileSync(path.join(tmpDir, 'notes.txt'), '');
  });

  afterEach(() => {
    fs.rmSync(tmpDir, { recursive: true, force: true });
  });

  test('lists only .json files', () => {
    const result = listJsonFiles(tmpDir);
    expect(result.ok).toBe(true);
    expect(result.files.sort()).toEqual(['show.json', 'x32.json']);
  });

  test('returns error for nonexistent dir', () => {
    const result = listJsonFiles('/nonexistent/dir');
    expect(result.ok).toBe(false);
    expect(result.error).toBeDefined();
  });
});

// ── parseBackendMessage ───────────────────────────────────────────────

describe('parseBackendMessage', () => {
  test('routes approval_request to approval:new', () => {
    const line = JSON.stringify({ type: 'approval_request', id: 'a1', action: 'fader' });
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('approval:new');
    expect(result.data.type).toBe('approval_request');
    expect(result.data.id).toBe('a1');
  });

  test('routes meter_update to meters:update', () => {
    const line = JSON.stringify({ type: 'meter_update', levels: [0.5, 0.3] });
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('meters:update');
  });

  test('routes llm_response to chat:llmResponse', () => {
    const line = JSON.stringify({ type: 'llm_response', message: 'boosting vocals' });
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('chat:llmResponse');
  });

  test('routes action_executed to approval:executed', () => {
    const line = JSON.stringify({ type: 'action_executed', id: 'a2' });
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('approval:executed');
  });

  test('routes unknown JSON type to backend:log', () => {
    const line = JSON.stringify({ type: 'unknown_type', data: 123 });
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('backend:log');
    expect(result.data.text).toBe(line);
  });

  test('routes non-JSON text to backend:log', () => {
    const line = '[2024-01-01] INFO: Starting engine...';
    const result = parseBackendMessage(line);
    expect(result.channel).toBe('backend:log');
    expect(result.data.text).toBe(line);
  });

  test('routes empty JSON object to backend:log', () => {
    const result = parseBackendMessage('{}');
    expect(result.channel).toBe('backend:log');
  });
});
