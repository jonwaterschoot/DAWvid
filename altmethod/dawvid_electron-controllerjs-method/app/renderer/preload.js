// renderer/preload.js
const { contextBridge, ipcRenderer } = require("electron");

contextBridge.exposeInMainWorld("dawvid", {
    // DAW → renderer
    onTransportUpdate: (cb) => ipcRenderer.on("transport-update", (_, data) => cb(data)),
    onDAWConnected:    (cb) => ipcRenderer.on("daw-connected",    () => cb()),
    onDAWDisconnected: (cb) => ipcRenderer.on("daw-disconnected", () => cb()),

    // renderer → DAW (via main)
    requestPlay:  () => ipcRenderer.send("request-play"),
    requestPause: () => ipcRenderer.send("request-pause"),
    requestSeek:  (pos) => ipcRenderer.send("request-seek", pos),
    sendMetadata: (data) => ipcRenderer.send("send-metadata", data),

    // File dialog
    openFile: () => ipcRenderer.invoke("open-file-dialog"),
});
