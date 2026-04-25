#pragma once

// ── Plugin identity ───────────────────────────────────────────────────────────
#define PLUGIN_ID      "com.dawvid.dawvid"
#define PLUGIN_NAME    "DAWvid"
#define PLUGIN_VENDOR  "Your Name"
#define PLUGIN_URL     "https://yourwebsite.com"
#define PLUGIN_VERSION "1.0.0"
#define PLUGIN_DESC    "Synced video playback for any DAW"

// ── GUI defaults ──────────────────────────────────────────────────────────────
constexpr uint32_t GUI_DEFAULT_WIDTH  = 960;
constexpr uint32_t GUI_DEFAULT_HEIGHT = 540;
constexpr uint32_t GUI_MIN_WIDTH      = 320;
constexpr uint32_t GUI_MIN_HEIGHT     = 180;

// ── Sync / timing ─────────────────────────────────────────────────────────────
// How often (in audio process() calls) we poll the transport for position drift.
// At 44100 Hz with 512-sample blocks this is ~86 checks/sec.
constexpr int SYNC_POLL_INTERVAL_BLOCKS = 1;

// If playhead drifts more than this many seconds, hard-seek the video.
constexpr double SYNC_SEEK_THRESHOLD_SEC = 0.050; // 50 ms
