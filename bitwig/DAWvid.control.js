// ─────────────────────────────────────────────────────────────────────────────
// DAWvid Bridge — Bitwig Controller Extension
//
// Companion script for the DAWvid CLAP plugin.  Runs inside Bitwig and gives
// the plugin two things it cannot get from the CLAP API alone:
//
//   1. Stopped-playhead updates — Bitwig only notifies CLAP plugins of the
//      transport position while playing.  This script observes
//      transport.playStartPosition() which fires on every caret move,
//      including when stopped, and forwards it to the plugin over UDP.
//
//   2. Bi-directional seek — when the user scrubs the video in the plugin GUI
//      the plugin sends "SEEK_BEATS <n>" over UDP and this script calls
//      transport.setPosition() to move the Bitwig playhead.
//
//   3. Tempo sync — sends the project BPM to the plugin on load and on every
//      change so beat↔seconds conversion is accurate before first play.
//
// Installation:
//   Copy this file to:
//     Windows : %USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\DAWvid\
//     macOS   : ~/Documents/Bitwig Studio/Controller Scripts/DAWvid/
//     Linux   : ~/Bitwig Studio/Controller Scripts/DAWvid/
//   Then in Bitwig → Settings → Controllers → Add controller → DAWvid → DAWvid Bridge.
//
// UDP ports (loopback only):
//   47491  — this script listens here (receives SEEK_BEATS / PLAY / STOP)
//   47492  — plugin listens here  (receives POS_BEATS / PLAYING / STOPPED)
// ─────────────────────────────────────────────────────────────────────────────

loadAPI(16);

host.defineController(
    "DAWvid",                                     // vendor
    "DAWvid Bridge",                              // name
    "1.0",                                        // version
    "8f3c2a7e-5b1d-4f6a-9d8c-3e7f2a1b5c0d",      // UUID — must be unique
    "DAWvid"                                      // author
);

// No MIDI ports needed — all communication is over UDP.
host.defineMidiPorts(0, 0);

// ─────────────────────────────────────────────────────────────────────────────
// Java class references
// Works in Bitwig 5+ (GraalVM).  If you are on an older Bitwig (Nashorn),
// replace Java.type("x.y.Z") with the bare x.y.Z namespace reference.
// ─────────────────────────────────────────────────────────────────────────────
var JDatagramSocket, JDatagramPacket, JInetAddress;
try {
    JDatagramSocket = Java.type("java.net.DatagramSocket");
    JDatagramPacket = Java.type("java.net.DatagramPacket");
    JInetAddress    = Java.type("java.net.InetAddress");
} catch (e) {
    // Nashorn fallback
    JDatagramSocket = java.net.DatagramSocket;
    JDatagramPacket = java.net.DatagramPacket;
    JInetAddress    = java.net.InetAddress;
}

var CTRL_PORT   = 47491;   // we listen here
var PLUGIN_PORT = 47492;   // plugin listens here

var transport;
var socket     = null;
var pluginAddr = null;

// ─────────────────────────────────────────────────────────────────────────────
// init() — called once when Bitwig loads the script
// ─────────────────────────────────────────────────────────────────────────────
function init()
{
    transport = host.createTransport();

    // ── Transport observers ──────────────────────────────────────────────────

    // playStartPosition() tracks where the blue caret sits in the Arranger,
    // and fires even when the transport is stopped.  This is the key fix for
    // the "Bitwig stopped-playhead" quirk.
    transport.playStartPosition().addValueObserver(function(beats) {
        sendToPlugin("POS_BEATS " + beats + "\n");
    });

    transport.isPlaying().addValueObserver(function(playing) {
        sendToPlugin(playing ? "PLAYING\n" : "STOPPED\n");
    });

    // Send BPM immediately on load and whenever it changes so the plugin can
    // convert beats↔seconds correctly before the transport has ever played.
    transport.tempo().addValueObserver(function(bpm) {
        sendToPlugin("TEMPO_BPM " + bpm + "\n");
    });

    // ── UDP socket ───────────────────────────────────────────────────────────
    try {
        socket = new JDatagramSocket(CTRL_PORT);
        socket.setSoTimeout(1);   // 1 ms receive timeout — avoids blocking the poll task
        pluginAddr = JInetAddress.getByName("127.0.0.1");
        println("DAWvid Bridge: listening on UDP port " + CTRL_PORT);
    } catch (e) {
        println("DAWvid Bridge: could not open socket on port " + CTRL_PORT + " — " + e);
        socket = null;
    }

    // Kick off the receive poll loop (reschedules itself every 20 ms).
    host.scheduleTask(pollIncoming, 20);

    println("DAWvid Bridge ready.");
}

// ─────────────────────────────────────────────────────────────────────────────
// pollIncoming() — called every 20 ms via scheduleTask
// ─────────────────────────────────────────────────────────────────────────────
function pollIncoming()
{
    if (socket !== null) {
        try {
            // Build a receive buffer using Java reflection (works in both
            // Nashorn and GraalVM without requiring Java.type("byte[]")).
            var buf = java.lang.reflect.Array.newInstance(
                          java.lang.Byte.TYPE, 256);
            var packet = new JDatagramPacket(buf, buf.length);
            socket.receive(packet);   // blocks for at most 1 ms (SO_TIMEOUT)

            var msg = new java.lang.String(
                          buf, 0, packet.getLength(), "UTF-8").trim() + "";
            handleCommand(msg);
        } catch (e) {
            // java.net.SocketTimeoutException is the normal case — ignore.
        }
    }

    // Reschedule unconditionally so the loop continues even after errors.
    host.scheduleTask(pollIncoming, 20);
}

// ─────────────────────────────────────────────────────────────────────────────
// handleCommand() — process a message received from the plugin
// ─────────────────────────────────────────────────────────────────────────────
function handleCommand(msg)
{
    if (msg.indexOf("SEEK_BEATS ") === 0) {
        var beats = parseFloat(msg.substring(11));
        if (!isNaN(beats)) {
            // Move the Bitwig arranger caret to the requested position.
            // transport.setPosition() takes beat-time (quarter notes from start).
            transport.setPosition(beats);
        }
    } else if (msg === "PLAY") {
        transport.play();
    } else if (msg === "STOP") {
        transport.stop();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// sendToPlugin() — send a UDP packet to the plugin on PLUGIN_PORT
// ─────────────────────────────────────────────────────────────────────────────
function sendToPlugin(msg)
{
    if (socket === null || pluginAddr === null) return;
    try {
        var bytes  = new java.lang.String(msg).getBytes("UTF-8");
        var packet = new JDatagramPacket(bytes, bytes.length, pluginAddr, PLUGIN_PORT);
        socket.send(packet);
    } catch (e) {
        // Ignore send failures (plugin not running yet, etc.)
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// exit() — called when Bitwig unloads the script
// ─────────────────────────────────────────────────────────────────────────────
function exit()
{
    if (socket !== null) {
        socket.close();
        socket = null;
    }
}

// Required by the Bitwig Controller API — nothing to flush for a pure UDP bridge.
function flush() {}
