// DAWvid.control.js — Bitwig Controller Script
// Drop this file into:
//   macOS:   ~/Documents/Bitwig Studio/Controller Scripts/DAWvid/
//   Windows: %USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\DAWvid\
//   Linux:   ~/Bitwig Studio/Controller Scripts/DAWvid/
// Then: Bitwig → Settings → Controllers → Add controller → Generic → DAWvid

loadAPI(17);

host.defineController(
    "DAWvid",          // vendor
    "DAWvid",          // name
    "1.0.0",           // version
    "a1b2c3d4-e5f6-7890-abcd-ef1234567890",  // UUID (generate your own)
    "DAWvid"           // author
);

host.defineMidiPorts(0, 0);

function println(s) {
    host.println(s);
}

// ── State ─────────────────────────────────────────────────────────────────────
var transport;
var connection  = null;
var connected   = false;

var lastPlaying  = false;
var lastPosition = 0.0;   // seconds
var lastTempo    = 120.0;

var isPlaying    = false;
var positionBeats = 0.0;
var tempoBPM      = 120.0;
var positionSec   = 0.0;

var incomingBuffer = "";

// ─────────────────────────────────────────────────────────────────────────────
function init() {
    host.setShouldFailOnDeprecatedUse(false);

    transport = host.createTransport();

    // Observe play state
    transport.isPlaying().markInterested();
    transport.isPlaying().addValueObserver(function(playing) {
        isPlaying = playing;
    });

    // Observe position in BEATS
    transport.playPosition().markInterested();
    transport.playPosition().addValueObserver(function(beats) {
        positionBeats = beats;
        // Calculate seconds: (beats * 60) / BPM
        positionSec = (tempoBPM > 0) ? (positionBeats * 60.0 / tempoBPM) : 0.0;
    });

    // Observe raw tempo (BPM)
    transport.tempo().value().markInterested();
    transport.tempo().value().addRawValueObserver(function(bpm) {
        tempoBPM = bpm;
        // Re-calculate seconds when tempo changes
        positionSec = (tempoBPM > 0) ? (positionBeats * 60.0 / tempoBPM) : 0.0;
    });

    // Connect to Electron
    attemptConnection();

    // Poll loop — sends state every 30 ms (increased for high-res sync)
    host.scheduleTask(pollLoop, 30);

    println("DAWvid controller initialised");
}

// ─────────────────────────────────────────────────────────────────────────────
function attemptConnection() {
    host.connectToRemoteHost("127.0.0.1", 9876, function(conn) {
        connection = conn;
        connected  = true;
        println("DAWvid: connected to Electron app");

        // Listen for messages coming back from Electron (Video → DAW)
        conn.setReceiveCallback(function(data) {
            try {
                // Use native Java String for robust UTF-8 handling
                var str = new java.lang.String(data, "UTF-8");
                incomingBuffer += str;
                
                var lines = incomingBuffer.split("\n");
                incomingBuffer = lines.pop(); // keep incomplete last line
                
                for (var i = 0; i < lines.length; i++) {
                    var line = lines[i].trim();
                    if (line) handleIncoming(line);
                }
            } catch (e) {
                println("DAWvid receive error: " + e);
            }
        });

        conn.setDisconnectCallback(function() {
            connected  = false;
            connection = null;
            println("DAWvid: disconnected — will retry");
            host.scheduleTask(attemptConnection, 3000);
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
function pollLoop() {
    if (connected && connection) {
        var posChanged    = Math.abs(positionSec - lastPosition) > 0.005; // 5ms precision
        var playChanged   = isPlaying !== lastPlaying;
        var tempoChanged  = Math.abs(tempoBPM - lastTempo) > 0.01;

        if (playChanged || posChanged || tempoChanged) {
            sendJSON({
                type:     "transport",
                playing:  isPlaying,
                position: positionSec,
                tempo:    tempoBPM
            });

            lastPlaying  = isPlaying;
            lastPosition = positionSec;
            lastTempo    = tempoBPM;
        }
    }

    host.scheduleTask(pollLoop, 30);
}

// ─────────────────────────────────────────────────────────────────────────────
function handleIncoming(jsonStr) {
    try {
        var msg = JSON.parse(jsonStr);
        
        switch (msg.type) {
            case "play":
                transport.play();
                break;
            case "pause":
            case "stop":
                transport.stop();
                break;
            case "seek":
                if (typeof msg.position === "number") {
                    // Convert seconds back to beats for Bitwig
                    var targetBeats = msg.position * tempoBPM / 60.0;
                    // Directly set play position (the blue triangle)
                    transport.playPosition().set(targetBeats);
                }
                break;
            case "metadata":
                if (msg.duration) {
                    var beats = msg.duration * tempoBPM / 60.0;
                    var bars  = beats / 4.0;
                    println("DAWvid: Video length is " + msg.duration.toFixed(2) + "s");
                    println("DAWvid: Length in beats: " + beats.toFixed(2) + " (~" + bars.toFixed(1) + " bars)");
                }
                break;
        }
    } catch (e) {
        println("DAWvid: JSON parse error: " + e + " on string: " + jsonStr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
function sendJSON(obj) {
    if (!connected || !connection) return;
    try {
        var str = JSON.stringify(obj) + "\n";
        var bytes = [];
        for (var i = 0; i < str.length; i++) {
            bytes.push(str.charCodeAt(i) & 0xFF);
        }
        connection.send(bytes);
    } catch (e) {
        connected = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
function flush() {}

function exit() {
    if (connection) {
        try { connection.disconnect(); } catch(e) {}
    }
    println("DAWvid: exit");
}
