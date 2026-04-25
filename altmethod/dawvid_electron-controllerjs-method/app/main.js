// main.js — Electron main process
const { app, BrowserWindow, ipcMain, dialog, Menu } = require("electron");
const net  = require("net");
const path = require("path");

// ── Config ────────────────────────────────────────────────────────────────────
const TCP_PORT     = 9876;
const SYNC_DRIFT_S = 0.05; // Tightened: 50ms drift before hard-seek

// ── State ─────────────────────────────────────────────────────────────────────
let mainWindow  = null;
let tcpServer   = null;
let dawClient   = null; // active TCP socket from Bitwig
let messageBuffer = "";

// ── Window ────────────────────────────────────────────────────────────────────
function createWindow() {
    mainWindow = new BrowserWindow({
        width:           960,
        height:          580,
        minWidth:        480,
        minHeight:       300,
        backgroundColor: "#0a0a0a",
        titleBarStyle:   "hiddenInset",  // clean look on macOS
        frame:           process.platform !== "darwin", // frameless on mac
        webPreferences: {
            nodeIntegration:     false,
            contextIsolation:    true,
            preload:             path.join(__dirname, "renderer", "preload.js"),
        },
    });

    mainWindow.loadFile(path.join(__dirname, "renderer", "index.html"));

    // Remove default menu bar
    Menu.setApplicationMenu(null);

    mainWindow.on("closed", () => { mainWindow = null; });
}

// ── TCP server (listens for the Bitwig controller script) ─────────────────────
function startTCPServer() {
    tcpServer = net.createServer((socket) => {
        console.log("[DAWvid] Bitwig controller connected");
        dawClient = socket;
        messageBuffer = "";

        // Tell the renderer we're connected
        sendToRenderer("daw-connected", {});

        socket.setEncoding("utf8");

        socket.on("data", (chunk) => {
            console.log(`[DAWvid] Raw data from Bitwig (${chunk.length} bytes)`);
            messageBuffer += chunk;
            // Messages are newline-delimited JSON
            const lines = messageBuffer.split("\n");
            messageBuffer = lines.pop(); // keep incomplete last line

            for (const line of lines) {
                if (line.trim()) handleDAWMessage(line.trim());
            }
        });

        socket.on("close", () => {
            console.log("[DAWvid] Bitwig controller disconnected");
            dawClient = null;
            sendToRenderer("daw-disconnected", {});
        });

        socket.on("error", (err) => {
            console.error("[DAWvid] Socket error:", err.message);
        });
    });

    tcpServer.listen(TCP_PORT, "127.0.0.1", () => {
        console.log(`[DAWvid] TCP server listening on port ${TCP_PORT}`);
    });

    tcpServer.on("error", (err) => {
        console.error("[DAWvid] Server error:", err.message);
    });
}

// ── Handle transport messages from Bitwig ─────────────────────────────────────
let lastKnownPosition = 0;
let lastKnownPlaying  = false;

function handleDAWMessage(raw) {
    let msg;
    try { 
        msg = JSON.parse(raw); 
    } catch (e) { 
        console.warn("[DAWvid] JSON parse error:", e.message, "Raw:", raw);
        return; 
    }

    if (msg.type === "transport") {
        console.log(`[DAWvid] Bitwig -> App: ${msg.playing ? "PLAY" : "STOP"} @ ${msg.position.toFixed(3)}s`);

        const posChanged   = Math.abs(msg.position - lastKnownPosition) > SYNC_DRIFT_S;
        const playChanged  = msg.playing !== lastKnownPlaying;

        lastKnownPosition = msg.position;
        lastKnownPlaying  = msg.playing;

        // Forward to renderer — it decides what to do with the video element
        sendToRenderer("transport-update", {
            playing:    msg.playing,
            position:   msg.position,
            tempo:      msg.tempo,
            posChanged, // hint: renderer should seek if true
            playChanged,
        });
    } else {
        console.log("[DAWvid] Received unknown message type:", msg.type);
    }
}

// ── IPC: renderer → main ──────────────────────────────────────────────────────

// User clicked Play/Pause in the app → tell Bitwig
ipcMain.on("request-play",  () => {
    console.log("[DAWvid] App -> Bitwig: PLAY");
    sendToDAW({ type: "play"  });
});
ipcMain.on("request-pause", () => {
    console.log("[DAWvid] App -> Bitwig: PAUSE");
    sendToDAW({ type: "pause" });
});
ipcMain.on("request-seek",  (_, pos) => {
    console.log(`[DAWvid] App -> Bitwig: SEEK ${pos.toFixed(3)}s`);
    sendToDAW({ type: "seek", position: pos });
});

ipcMain.on("send-metadata", (_, data) => {
    console.log("[DAWvid] App -> Bitwig: METADATA", data);
    sendToDAW({ type: "metadata", ...data });
});

// Open file dialog
ipcMain.handle("open-file-dialog", async () => {
    const result = await dialog.showOpenDialog(mainWindow, {
        title:      "Open Video",
        properties: ["openFile"],
        filters: [
            { name: "Video files", extensions: ["mp4", "mkv", "mov", "avi", "webm", "m4v"] },
            { name: "All files",   extensions: ["*"] },
        ],
    });
    if (!result.canceled) {
        console.log("[DAWvid] Loading video:", result.filePaths[0]);
    }
    return result.canceled ? null : result.filePaths[0];
});

// ── Helpers ───────────────────────────────────────────────────────────────────
function sendToRenderer(channel, data) {
    if (mainWindow && !mainWindow.isDestroyed()) {
        mainWindow.webContents.send(channel, data);
    }
}

function sendToDAW(obj) {
    if (dawClient && !dawClient.destroyed) {
        const payload = JSON.stringify(obj) + "\n";
        dawClient.write(payload);
    } else {
        console.warn("[DAWvid] Cannot send to DAW: not connected");
    }
}

// ── App lifecycle ─────────────────────────────────────────────────────────────
app.whenReady().then(() => {
    createWindow();
    startTCPServer();
});

app.on("window-all-closed", () => {
    if (tcpServer) tcpServer.close();
    if (process.platform !== "darwin") app.quit();
});

app.on("activate", () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow();
});
