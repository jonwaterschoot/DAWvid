// renderer/player.js
// Runs in the Electron renderer process (sandboxed).
// Communicates with main.js via window.dawvid (preload bridge).

"use strict";

// ── Elements ──────────────────────────────────────────────────────────────────
const video       = document.getElementById("video");
const btnPlay     = document.getElementById("btn-play");
const timecode    = document.getElementById("timecode");
const duration    = document.getElementById("duration");
const scrubTrack  = document.getElementById("scrub-track");
const scrubFill   = document.getElementById("scrub-fill");
const scrubThumb  = document.getElementById("scrub-thumb");
const statusDot   = document.getElementById("status-dot");
const statusLabel = document.getElementById("status-label");
const dropOverlay = document.getElementById("drop-overlay");
const syncFlash   = document.getElementById("sync-flash");
const btnOpen     = document.getElementById("btn-open");
const openLink    = document.getElementById("open-link");
const volSlider   = document.getElementById("vol-slider");

// ── State ─────────────────────────────────────────────────────────────────────
const SEEK_THRESHOLD   = 0.15;   // seconds drift before hard-seeking
const PLAY_PAUSE_GUARD = 300;    // ms — ignore DAW play/pause right after user click
let lastUserInteraction = 0;
let dawConnected        = false;
let scrubbing           = false;
let userInitiatedAction = false; // true when user clicked play in this app

// ── Helpers ───────────────────────────────────────────────────────────────────
function formatTime(sec) {
    if (!isFinite(sec)) return "--:--:--";
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = Math.floor(sec % 60);
    return [h, m, s].map(n => String(n).padStart(2, "0")).join(":");
}

function flashSync() {
    syncFlash.classList.remove("active");
    void syncFlash.offsetWidth; // reflow
    syncFlash.classList.add("active");
}

function updateScrub() {
    if (!scrubbing) {
        const pct = video.duration ? (video.currentTime / video.duration) * 100 : 0;
        scrubFill.style.width  = pct + "%";
        scrubThumb.style.left  = pct + "%";
    }
    timecode.textContent = formatTime(video.currentTime);
}

function setPlayUI(playing) {
    btnPlay.classList.toggle("playing", playing);
}

// ── DAW transport updates (main → renderer) ───────────────────────────────────
window.dawvid.onTransportUpdate((data) => {
    const now = Date.now();

    // Guard: ignore DAW messages briefly after user clicked play/pause here
    // to avoid immediate flip-flop
    if (now - lastUserInteraction < PLAY_PAUSE_GUARD) return;

    // ── Play / pause ──────────────────────────────────────────────────────────
    if (data.playChanged) {
        if (data.playing && video.paused) {
            video.play().catch(() => {});
        } else if (!data.playing && !video.paused) {
            video.pause();
        }
        setPlayUI(data.playing);
    }

    // ── Position sync ─────────────────────────────────────────────────────────
    if (data.posChanged && video.duration) {
        const drift = Math.abs(video.currentTime - data.position);
        if (drift > SEEK_THRESHOLD) {
            video.currentTime = data.position;
            flashSync();
        }
    }
});

window.dawvid.onDAWConnected(() => {
    dawConnected = true;
    statusDot.className   = "connected";
    statusLabel.textContent = "Bitwig";
});

window.dawvid.onDAWDisconnected(() => {
    dawConnected = false;
    statusDot.className   = "disconnected";
    statusLabel.textContent = "No DAW";
});

// ── Play / pause button ───────────────────────────────────────────────────────
btnPlay.addEventListener("click", () => {
    lastUserInteraction = Date.now();
    userInitiatedAction = true;

    if (video.paused) {
        video.play().then(() => {
            setPlayUI(true);
            if (dawConnected) window.dawvid.requestPlay();
        }).catch(() => {});
    } else {
        video.pause();
        setPlayUI(false);
        if (dawConnected) window.dawvid.requestPause();
    }
});

// ── Video element events ──────────────────────────────────────────────────────
video.addEventListener("loadedmetadata", () => {
    duration.textContent = formatTime(video.duration);
    dropOverlay.classList.add("hidden");
    updateScrub();

    // Send metadata to Bitwig so it can log the length in beats
    if (dawConnected) {
        window.dawvid.sendMetadata({ duration: video.duration });
    }
});

video.addEventListener("timeupdate", updateScrub);

video.addEventListener("ended", () => {
    setPlayUI(false);
    if (dawConnected) window.dawvid.requestPause();
});

// ── Scrub bar ─────────────────────────────────────────────────────────────────
function scrubTo(e) {
    const rect = scrubTrack.getBoundingClientRect();
    const pct  = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
    const pos  = pct * (video.duration || 0);
    video.currentTime = pos;
    updateScrub();
    return pos;
}

scrubTrack.addEventListener("mousedown", (e) => {
    scrubbing = true;
    scrubTo(e);
});
document.addEventListener("mousemove", (e) => { if (scrubbing) scrubTo(e); });
document.addEventListener("mouseup", (e) => {
    if (scrubbing) {
        const pos = scrubTo(e);
        scrubbing = false;
        if (dawConnected) window.dawvid.requestSeek(pos);
    }
});

// ── Volume ────────────────────────────────────────────────────────────────────
volSlider.addEventListener("input", () => {
    video.volume = parseFloat(volSlider.value);
});

// ── File opening ──────────────────────────────────────────────────────────────
async function openFile() {
    const filePath = await window.dawvid.openFile();
    if (filePath) loadVideo(filePath);
}

function loadVideo(filePath) {
    // Electron can load local files directly via file:// URLs
    video.src = "file://" + filePath.replace(/\\/g, "/");
    video.load();
    video.currentTime = 0;
    setPlayUI(false);
}

btnOpen.addEventListener("click", openFile);
openLink.addEventListener("click", openFile);

// ── Drag and drop files ───────────────────────────────────────────────────────
document.addEventListener("dragover", (e) => {
    e.preventDefault();
    document.body.classList.add("drag-over");
});
document.addEventListener("dragleave", (e) => {
    if (!e.relatedTarget) document.body.classList.remove("drag-over");
});
document.addEventListener("drop", (e) => {
    e.preventDefault();
    document.body.classList.remove("drag-over");
    const file = e.dataTransfer.files[0];
    if (file) loadVideo(file.path);
});

// ── Keyboard shortcuts ────────────────────────────────────────────────────────
document.addEventListener("keydown", (e) => {
    if (e.code === "Space") {
        e.preventDefault();
        btnPlay.click();
    }
    if (e.code === "ArrowRight") {
        video.currentTime = Math.min(video.duration || 0, video.currentTime + 5);
        if (dawConnected) window.dawvid.requestSeek(video.currentTime);
    }
    if (e.code === "ArrowLeft") {
        video.currentTime = Math.max(0, video.currentTime - 5);
        if (dawConnected) window.dawvid.requestSeek(video.currentTime);
    }
});
