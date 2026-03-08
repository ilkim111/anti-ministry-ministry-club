const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('mixagent', {
  // Config
  config: {
    load: ()                    => ipcRenderer.invoke('config:load'),
    save: (config)              => ipcRenderer.invoke('config:save', config),
    loadEnv: ()                 => ipcRenderer.invoke('config:loadEnv'),
    saveEnv: (env)              => ipcRenderer.invoke('config:saveEnv', env),
    listConfigs: ()             => ipcRenderer.invoke('config:listConsoleConfigs'),
    loadFile: (filename)        => ipcRenderer.invoke('config:loadFile', filename),
  },

  // Backend process
  backend: {
    start: ()                   => ipcRenderer.invoke('backend:start'),
    stop: ()                    => ipcRenderer.invoke('backend:stop'),
    status: ()                  => ipcRenderer.invoke('backend:status'),
    onLog: (cb)                 => ipcRenderer.on('backend:log', (_e, data) => cb(data)),
    onStopped: (cb)             => ipcRenderer.on('backend:stopped', (_e, data) => cb(data)),
  },

  // Approval queue
  approval: {
    approve: (id)               => ipcRenderer.invoke('approval:approve', id),
    reject: (id)                => ipcRenderer.invoke('approval:reject', id),
    approveAll: ()              => ipcRenderer.invoke('approval:approveAll'),
    rejectAll: ()               => ipcRenderer.invoke('approval:rejectAll'),
    setMode: (mode)             => ipcRenderer.invoke('approval:setMode', mode),
    onNew: (cb)                 => ipcRenderer.on('approval:new', (_e, data) => cb(data)),
    onExecuted: (cb)            => ipcRenderer.on('approval:executed', (_e, data) => cb(data)),
  },

  // Chat
  chat: {
    send: (message)             => ipcRenderer.invoke('chat:send', message),
    onResponse: (cb)            => ipcRenderer.on('chat:llmResponse', (_e, data) => cb(data)),
  },

  // Meters
  meters: {
    onUpdate: (cb)              => ipcRenderer.on('meters:update', (_e, data) => cb(data)),
  },

  // Ollama
  ollama: {
    testConnection: (host)      => ipcRenderer.invoke('ollama:testConnection', host),
  },

  // Console connection test
  consoleTest: {
    testConnection: (opts)      => ipcRenderer.invoke('console:testConnection', opts),
  },

  // Audio device detection
  audio: {
    testDevices: ()             => ipcRenderer.invoke('audio:testDevices'),
  },

  // Demo mode
  demo: {
    start: ()                   => ipcRenderer.invoke('demo:start'),
    stop: ()                    => ipcRenderer.invoke('demo:stop'),
    onTick: (cb)                => ipcRenderer.on('demo:tick', (_e, data) => cb(data)),
    onStopped: (cb)             => ipcRenderer.on('demo:stopped', () => cb()),
  },

  // Auto-updater
  updater: {
    check: ()                   => ipcRenderer.invoke('updater:check'),
    download: ()                => ipcRenderer.invoke('updater:download'),
    install: ()                 => ipcRenderer.invoke('updater:install'),
    onAvailable: (cb)           => ipcRenderer.on('updater:available', (_e, data) => cb(data)),
    onUpToDate: (cb)            => ipcRenderer.on('updater:upToDate', () => cb()),
    onProgress: (cb)            => ipcRenderer.on('updater:progress', (_e, data) => cb(data)),
    onReady: (cb)               => ipcRenderer.on('updater:ready', () => cb()),
    onError: (cb)               => ipcRenderer.on('updater:error', (_e, data) => cb(data)),
  },
});
