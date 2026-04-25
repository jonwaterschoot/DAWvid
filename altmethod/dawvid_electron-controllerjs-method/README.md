# DAWvid

DAW-synced video player. Load a video, open a session in Bitwig, and playback tries to stay in lock-step.
While developping this i found it pretty hard to get it to smoothly follow along, mostly it comes down to not quickly enough updating. Most reliable way to use this at the moment is to use Bitwig only as playback control. And the video only as an indicator of where in the audio the playbackhead is. It only updates when the blue triangular playhead changes position. Using the time selector does repostion the playhead, but overall. It's not as usable as i had hoped.

The Clap plugin by the same name i've built should work a bit better and doesn't need to run the app separetly.


## How it works

```
Bitwig Controller Script  ──TCP:9876──►  Electron App
        ◄──────────────────────────────
```

- **Controller Script** polls Bitwig's transport every 50 ms, sends JSON over TCP
- **Electron app** runs a TCP server, feeds position/play state to an HTML5 `<video>` element
- Two-way: clicking Play in DAWvid sends a message back → Bitwig starts rolling

No FFmpeg. No DLLs. No native compilation.

---

## Setup

### 1. Install the controller script in Bitwig

Copy the `controller/DAWvid.control.js` file into a new folder:

| Platform | Path |
|---|---|
| macOS   | `~/Documents/Bitwig Studio/Controller Scripts/DAWvid/` |
| Windows | `%USERPROFILE%\Documents\Bitwig Studio\Controller Scripts\DAWvid\` |
| Linux   | `~/Bitwig Studio/Controller Scripts/DAWvid/` |

Then in Bitwig: **Settings → Controllers → Add controller → Generic → DAWvid**

### 2. Run the Electron app

```bash
cd app
npm install
npm start
```

### 3. Connect

Start DAWvid first (it listens on TCP port 9876), then load the controller in Bitwig. The status dot in the bottom-right corner turns **yellow-green** when Bitwig is connected.

---

## Usage

| Action | Result |
|---|---|
| Drag & drop video file | Loads the video |
| Click **Open** | File browser |
| **Space** | Play / Pause (also controls Bitwig) |
| **← →** arrows | Seek ±5 seconds (also seeks Bitwig) |
| Scrub bar | Seek video + sends position to Bitwig |
| Bitwig play/pause/seek | Video follows automatically |

---

## Build a distributable

```bash
cd app
npm run build
```

Outputs: `.dmg` (macOS), `.exe` installer (Windows), `.AppImage` (Linux)

---

## Notes

### Tempo mapping in the controller script
Bitwig normalises the tempo parameter to a 0–1 range. The conversion in the script (`20 + rawValue * 646`) is an approximation. For exact BPM values, check the Bitwig Controller API docs for your version — there may be a direct `getBpm()` method available.

### Two-way seek limitation
CLAP and most DAW controller APIs let plugins/scripts trigger **play/stop** reliably. Seeking the DAW from external code is less standardised — `transport.setPosition()` in the Bitwig Controller Script API should work but verify it in your Bitwig version.

### Port conflict
If something else is already on port 9876, change `TCP_PORT` in `main.js` and `port` in `connectToRemoteHost()` in the controller script to match.

### Other DAWs
The controller script is Bitwig-specific (uses Bitwig's JS Controller API). For Reaper you'd write a ReaScript; for Ableton a Max for Live device. The Electron app side is completely DAW-agnostic — just send the same JSON format over TCP.
