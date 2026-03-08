/**
 * Sets up auto-updater event handlers and periodic checks.
 * Extracted from main.js for testability.
 *
 * @param {object} autoUpdater - electron-updater's autoUpdater instance
 * @param {object} options
 * @param {Function} options.sendToRenderer - (channel, data) => void
 * @param {Function} options.stopBackend - () => void
 * @param {boolean} options.isPackaged - whether the app is packaged
 * @param {number} [options.checkIntervalMs=1800000] - ms between update checks
 * @returns {{ dispose: Function }} cleanup handle
 */
function setupAutoUpdater(autoUpdater, { sendToRenderer, stopBackend, isPackaged, checkIntervalMs = 30 * 60 * 1000 }) {
  if (!isPackaged) {
    return { dispose: () => {} };
  }

  autoUpdater.autoDownload = false;
  autoUpdater.autoInstallOnAppQuit = true;

  autoUpdater.on('update-available', (info) => {
    sendToRenderer('updater:available', {
      version: info.version,
      releaseNotes: info.releaseNotes || '',
      releaseDate: info.releaseDate || ''
    });
  });

  autoUpdater.on('update-not-available', () => {
    sendToRenderer('updater:upToDate', undefined);
  });

  autoUpdater.on('download-progress', (progress) => {
    sendToRenderer('updater:progress', {
      percent: Math.round(progress.percent),
      transferred: progress.transferred,
      total: progress.total
    });
  });

  autoUpdater.on('update-downloaded', () => {
    sendToRenderer('updater:ready', undefined);
  });

  autoUpdater.on('error', (err) => {
    sendToRenderer('updater:error', { error: err.message });
  });

  // Check on setup, then periodically
  autoUpdater.checkForUpdates().catch(() => {});
  const intervalId = setInterval(() => {
    autoUpdater.checkForUpdates().catch(() => {});
  }, checkIntervalMs);

  return {
    dispose: () => clearInterval(intervalId)
  };
}

/**
 * Handle updater:check IPC call.
 */
async function handleCheckForUpdates(autoUpdater) {
  try {
    const result = await autoUpdater.checkForUpdates();
    return { ok: true, version: result?.updateInfo?.version };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

/**
 * Handle updater:download IPC call.
 */
async function handleDownloadUpdate(autoUpdater) {
  try {
    await autoUpdater.downloadUpdate();
    return { ok: true };
  } catch (err) {
    return { ok: false, error: err.message };
  }
}

/**
 * Handle updater:install IPC call.
 */
function handleInstallUpdate(autoUpdater, stopBackend) {
  stopBackend();
  autoUpdater.quitAndInstall();
}

module.exports = {
  setupAutoUpdater,
  handleCheckForUpdates,
  handleDownloadUpdate,
  handleInstallUpdate
};
