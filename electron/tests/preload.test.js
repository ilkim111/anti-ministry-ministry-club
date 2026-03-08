/**
 * Tests for preload.js — verifies the IPC API surface exposed to the renderer.
 * We mock Electron's contextBridge and ipcRenderer to capture what gets exposed.
 */

let exposedApi = {};
const registeredListeners = {};
const invokedChannels = [];

// Mock electron modules before requiring preload
jest.mock('electron', () => ({
  contextBridge: {
    exposeInMainWorld: jest.fn((name, api) => {
      exposedApi = api;
    })
  },
  ipcRenderer: {
    invoke: jest.fn((channel, ...args) => {
      invokedChannels.push({ channel, args });
      return Promise.resolve({ ok: true });
    }),
    on: jest.fn((channel, cb) => {
      registeredListeners[channel] = cb;
    })
  }
}));

beforeAll(() => {
  require('../preload');
});

beforeEach(() => {
  invokedChannels.length = 0;
});

describe('preload API surface', () => {
  test('exposes API under "mixagent" namespace', () => {
    const { contextBridge } = require('electron');
    expect(contextBridge.exposeInMainWorld).toHaveBeenCalledWith('mixagent', expect.any(Object));
  });

  test('has all expected top-level namespaces', () => {
    const namespaces = Object.keys(exposedApi);
    expect(namespaces).toContain('config');
    expect(namespaces).toContain('backend');
    expect(namespaces).toContain('approval');
    expect(namespaces).toContain('chat');
    expect(namespaces).toContain('meters');
    expect(namespaces).toContain('ollama');
    expect(namespaces).toContain('updater');
  });
});

describe('config namespace', () => {
  test('has load, save, loadEnv, saveEnv, listConfigs, loadFile', () => {
    const methods = Object.keys(exposedApi.config);
    expect(methods).toEqual(expect.arrayContaining([
      'load', 'save', 'loadEnv', 'saveEnv', 'listConfigs', 'loadFile'
    ]));
  });

  test('load invokes config:load', async () => {
    await exposedApi.config.load();
    expect(invokedChannels).toContainEqual({ channel: 'config:load', args: [] });
  });

  test('save invokes config:save with config object', async () => {
    const config = { console_type: 'x32' };
    await exposedApi.config.save(config);
    expect(invokedChannels).toContainEqual({ channel: 'config:save', args: [config] });
  });
});

describe('backend namespace', () => {
  test('has start, stop, status, onLog, onStopped', () => {
    const methods = Object.keys(exposedApi.backend);
    expect(methods).toEqual(expect.arrayContaining([
      'start', 'stop', 'status', 'onLog', 'onStopped'
    ]));
  });

  test('start invokes backend:start', async () => {
    await exposedApi.backend.start();
    expect(invokedChannels).toContainEqual({ channel: 'backend:start', args: [] });
  });

  test('onLog registers listener on backend:log', () => {
    const cb = jest.fn();
    exposedApi.backend.onLog(cb);
    expect(registeredListeners['backend:log']).toBeDefined();
  });
});

describe('approval namespace', () => {
  test('has approve, reject, approveAll, rejectAll, setMode, onNew, onExecuted', () => {
    const methods = Object.keys(exposedApi.approval);
    expect(methods).toEqual(expect.arrayContaining([
      'approve', 'reject', 'approveAll', 'rejectAll', 'setMode', 'onNew', 'onExecuted'
    ]));
  });

  test('approve invokes approval:approve with action id', async () => {
    await exposedApi.approval.approve('action-123');
    expect(invokedChannels).toContainEqual({ channel: 'approval:approve', args: ['action-123'] });
  });

  test('setMode invokes approval:setMode with mode', async () => {
    await exposedApi.approval.setMode('auto_urgent');
    expect(invokedChannels).toContainEqual({ channel: 'approval:setMode', args: ['auto_urgent'] });
  });
});

describe('chat namespace', () => {
  test('has send and onResponse', () => {
    expect(exposedApi.chat.send).toBeDefined();
    expect(exposedApi.chat.onResponse).toBeDefined();
  });

  test('send invokes chat:send with message', async () => {
    await exposedApi.chat.send('bring up the vocals');
    expect(invokedChannels).toContainEqual({
      channel: 'chat:send',
      args: ['bring up the vocals']
    });
  });
});

describe('updater namespace', () => {
  test('has check, download, install, onAvailable, onUpToDate, onProgress, onReady, onError', () => {
    const methods = Object.keys(exposedApi.updater);
    expect(methods).toEqual(expect.arrayContaining([
      'check', 'download', 'install',
      'onAvailable', 'onUpToDate', 'onProgress', 'onReady', 'onError'
    ]));
  });

  test('check invokes updater:check', async () => {
    await exposedApi.updater.check();
    expect(invokedChannels).toContainEqual({ channel: 'updater:check', args: [] });
  });

  test('download invokes updater:download', async () => {
    await exposedApi.updater.download();
    expect(invokedChannels).toContainEqual({ channel: 'updater:download', args: [] });
  });

  test('install invokes updater:install', async () => {
    await exposedApi.updater.install();
    expect(invokedChannels).toContainEqual({ channel: 'updater:install', args: [] });
  });

  test('onAvailable registers listener on updater:available', () => {
    const cb = jest.fn();
    exposedApi.updater.onAvailable(cb);
    expect(registeredListeners['updater:available']).toBeDefined();
  });

  test('onReady registers listener on updater:ready', () => {
    const cb = jest.fn();
    exposedApi.updater.onReady(cb);
    expect(registeredListeners['updater:ready']).toBeDefined();
  });

  test('onProgress registers listener on updater:progress', () => {
    const cb = jest.fn();
    exposedApi.updater.onProgress(cb);
    expect(registeredListeners['updater:progress']).toBeDefined();
  });

  test('onError registers listener on updater:error', () => {
    const cb = jest.fn();
    exposedApi.updater.onError(cb);
    expect(registeredListeners['updater:error']).toBeDefined();
  });

  test('event listeners unwrap ipcRenderer event parameter', () => {
    const cb = jest.fn();
    exposedApi.updater.onAvailable(cb);

    // Simulate ipcRenderer.on callback which includes (_event, data)
    const handler = registeredListeners['updater:available'];
    handler({}, { version: '2.0.0' });

    expect(cb).toHaveBeenCalledWith({ version: '2.0.0' });
  });
});

describe('meters namespace', () => {
  test('has onUpdate', () => {
    expect(exposedApi.meters.onUpdate).toBeDefined();
  });
});

describe('ollama namespace', () => {
  test('has testConnection', () => {
    expect(exposedApi.ollama.testConnection).toBeDefined();
  });

  test('testConnection invokes ollama:testConnection with host', async () => {
    await exposedApi.ollama.testConnection('http://localhost:11434');
    expect(invokedChannels).toContainEqual({
      channel: 'ollama:testConnection',
      args: ['http://localhost:11434']
    });
  });
});
