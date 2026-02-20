"use strict";
const electron = require("electron");
const versions = {};
for (const type of ["chrome", "node", "electron"]) {
  versions[type] = process.versions[type];
}
function validateIPC(channel) {
  if (!channel) {
    throw new Error(`Unsupported event IPC channel '${channel}'`);
  }
  return true;
}
const globals = {
  /** Processes versions **/
  versions,
  /**
   * A minimal set of methods exposed from Electron's `ipcRenderer`
   * to support communication to main process.
   */
  ipcRenderer: {
    send(channel, ...args) {
      if (validateIPC(channel)) {
        electron.ipcRenderer.send(channel, ...args);
      }
    },
    invoke(channel, ...args) {
      if (validateIPC(channel)) {
        return electron.ipcRenderer.invoke(channel, ...args);
      }
    },
    on(channel, listener) {
      if (validateIPC(channel)) {
        electron.ipcRenderer.on(channel, listener);
        return this;
      }
    },
    once(channel, listener) {
      if (validateIPC(channel)) {
        electron.ipcRenderer.once(channel, listener);
        return this;
      }
    },
    removeListener(channel, listener) {
      if (validateIPC(channel)) {
        electron.ipcRenderer.removeListener(channel, listener);
        return this;
      }
    }
  },
  // Add a specific function to open the camera window
  openCameraWindow: () => {
    electron.ipcRenderer.send("open-camera-window");
  }
};
electron.contextBridge.exposeInMainWorld("electron", globals);
