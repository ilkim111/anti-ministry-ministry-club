const {
  setupAutoUpdater,
  handleCheckForUpdates,
  handleDownloadUpdate,
  handleInstallUpdate
} = require('../lib/updater');

// ── Helper: create a mock autoUpdater with EventEmitter pattern ──────

function createMockAutoUpdater() {
  const listeners = {};
  return {
    autoDownload: undefined,
    autoInstallOnAppQuit: undefined,
    on: jest.fn((event, cb) => {
      listeners[event] = cb;
    }),
    checkForUpdates: jest.fn(() => Promise.resolve({ updateInfo: { version: '2.0.0' } })),
    downloadUpdate: jest.fn(() => Promise.resolve()),
    quitAndInstall: jest.fn(),
    // Test helper: emit an event
    _emit: (event, data) => {
      if (listeners[event]) listeners[event](data);
    },
    _listeners: listeners
  };
}

// ── setupAutoUpdater ─────────────────────────────────────────────────

describe('setupAutoUpdater', () => {
  beforeEach(() => {
    jest.useFakeTimers();
  });

  afterEach(() => {
    jest.useRealTimers();
  });

  test('skips setup when not packaged', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    const handle = setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: false
    });

    expect(mockUpdater.on).not.toHaveBeenCalled();
    expect(mockUpdater.checkForUpdates).not.toHaveBeenCalled();
    handle.dispose(); // should be a no-op
  });

  test('configures autoDownload=false and autoInstallOnAppQuit=true', () => {
    const mockUpdater = createMockAutoUpdater();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer: jest.fn(),
      stopBackend: jest.fn(),
      isPackaged: true
    });

    expect(mockUpdater.autoDownload).toBe(false);
    expect(mockUpdater.autoInstallOnAppQuit).toBe(true);
  });

  test('registers all expected event handlers', () => {
    const mockUpdater = createMockAutoUpdater();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer: jest.fn(),
      stopBackend: jest.fn(),
      isPackaged: true
    });

    const events = mockUpdater.on.mock.calls.map(([event]) => event);
    expect(events).toContain('update-available');
    expect(events).toContain('update-not-available');
    expect(events).toContain('download-progress');
    expect(events).toContain('update-downloaded');
    expect(events).toContain('error');
  });

  test('checks for updates immediately on setup', () => {
    const mockUpdater = createMockAutoUpdater();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer: jest.fn(),
      stopBackend: jest.fn(),
      isPackaged: true
    });

    expect(mockUpdater.checkForUpdates).toHaveBeenCalledTimes(1);
  });

  test('checks for updates periodically', () => {
    const mockUpdater = createMockAutoUpdater();
    const intervalMs = 1000; // short interval for test

    const handle = setupAutoUpdater(mockUpdater, {
      sendToRenderer: jest.fn(),
      stopBackend: jest.fn(),
      isPackaged: true,
      checkIntervalMs: intervalMs
    });

    expect(mockUpdater.checkForUpdates).toHaveBeenCalledTimes(1);

    jest.advanceTimersByTime(intervalMs);
    expect(mockUpdater.checkForUpdates).toHaveBeenCalledTimes(2);

    jest.advanceTimersByTime(intervalMs);
    expect(mockUpdater.checkForUpdates).toHaveBeenCalledTimes(3);

    handle.dispose();
    jest.advanceTimersByTime(intervalMs * 5);
    // Should stay at 3 since interval was cleared
    expect(mockUpdater.checkForUpdates).toHaveBeenCalledTimes(3);
  });

  test('update-available sends version info to renderer', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('update-available', {
      version: '2.1.0',
      releaseNotes: 'Bug fixes',
      releaseDate: '2024-01-15'
    });

    expect(sendToRenderer).toHaveBeenCalledWith('updater:available', {
      version: '2.1.0',
      releaseNotes: 'Bug fixes',
      releaseDate: '2024-01-15'
    });
  });

  test('update-available handles missing optional fields', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('update-available', { version: '2.0.0' });

    expect(sendToRenderer).toHaveBeenCalledWith('updater:available', {
      version: '2.0.0',
      releaseNotes: '',
      releaseDate: ''
    });
  });

  test('update-not-available notifies renderer', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('update-not-available');
    expect(sendToRenderer).toHaveBeenCalledWith('updater:upToDate', undefined);
  });

  test('download-progress rounds percent and forwards to renderer', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('download-progress', {
      percent: 42.7893,
      transferred: 5000000,
      total: 12000000
    });

    expect(sendToRenderer).toHaveBeenCalledWith('updater:progress', {
      percent: 43,
      transferred: 5000000,
      total: 12000000
    });
  });

  test('update-downloaded sends ready notification', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('update-downloaded');
    expect(sendToRenderer).toHaveBeenCalledWith('updater:ready', undefined);
  });

  test('error sends error message to renderer', () => {
    const mockUpdater = createMockAutoUpdater();
    const sendToRenderer = jest.fn();

    setupAutoUpdater(mockUpdater, {
      sendToRenderer,
      stopBackend: jest.fn(),
      isPackaged: true
    });

    mockUpdater._emit('error', new Error('Network timeout'));
    expect(sendToRenderer).toHaveBeenCalledWith('updater:error', {
      error: 'Network timeout'
    });
  });

  test('handles checkForUpdates rejection gracefully', () => {
    const mockUpdater = createMockAutoUpdater();
    mockUpdater.checkForUpdates = jest.fn(() => Promise.reject(new Error('offline')));

    // Should not throw
    expect(() => {
      setupAutoUpdater(mockUpdater, {
        sendToRenderer: jest.fn(),
        stopBackend: jest.fn(),
        isPackaged: true
      });
    }).not.toThrow();
  });
});

// ── handleCheckForUpdates ────────────────────────────────────────────

describe('handleCheckForUpdates', () => {
  test('returns version on success', async () => {
    const mockUpdater = {
      checkForUpdates: jest.fn(() =>
        Promise.resolve({ updateInfo: { version: '3.0.0' } })
      )
    };

    const result = await handleCheckForUpdates(mockUpdater);
    expect(result).toEqual({ ok: true, version: '3.0.0' });
  });

  test('returns error on failure', async () => {
    const mockUpdater = {
      checkForUpdates: jest.fn(() =>
        Promise.reject(new Error('No internet'))
      )
    };

    const result = await handleCheckForUpdates(mockUpdater);
    expect(result).toEqual({ ok: false, error: 'No internet' });
  });

  test('handles null result gracefully', async () => {
    const mockUpdater = {
      checkForUpdates: jest.fn(() => Promise.resolve(null))
    };

    const result = await handleCheckForUpdates(mockUpdater);
    expect(result.ok).toBe(true);
    expect(result.version).toBeUndefined();
  });
});

// ── handleDownloadUpdate ─────────────────────────────────────────────

describe('handleDownloadUpdate', () => {
  test('returns ok on success', async () => {
    const mockUpdater = {
      downloadUpdate: jest.fn(() => Promise.resolve())
    };

    const result = await handleDownloadUpdate(mockUpdater);
    expect(result).toEqual({ ok: true });
    expect(mockUpdater.downloadUpdate).toHaveBeenCalledTimes(1);
  });

  test('returns error on failure', async () => {
    const mockUpdater = {
      downloadUpdate: jest.fn(() =>
        Promise.reject(new Error('Disk full'))
      )
    };

    const result = await handleDownloadUpdate(mockUpdater);
    expect(result).toEqual({ ok: false, error: 'Disk full' });
  });
});

// ── handleInstallUpdate ──────────────────────────────────────────────

describe('handleInstallUpdate', () => {
  test('stops backend before quitting and installing', () => {
    const mockUpdater = { quitAndInstall: jest.fn() };
    const stopBackend = jest.fn();

    handleInstallUpdate(mockUpdater, stopBackend);

    expect(stopBackend).toHaveBeenCalledTimes(1);
    expect(mockUpdater.quitAndInstall).toHaveBeenCalledTimes(1);

    // stopBackend must be called before quitAndInstall
    const stopOrder = stopBackend.mock.invocationCallOrder[0];
    const quitOrder = mockUpdater.quitAndInstall.mock.invocationCallOrder[0];
    expect(stopOrder).toBeLessThan(quitOrder);
  });
});
