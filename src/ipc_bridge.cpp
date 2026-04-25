#include "ipc_bridge.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>   // strtod

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2tcpip.h>
// ws2_32.lib linked via CMakeLists.txt
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
#  include <cerrno>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Platform shims
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
static inline SOCKET   nativeSock(uintptr_t s) { return static_cast<SOCKET>(s); }
static inline void     closeSocket(uintptr_t s) { closesocket(nativeSock(s)); }
static inline bool     isTimeout()   { return WSAGetLastError() == WSAETIMEDOUT; }
static inline bool     isInterrupt() { return false; }
#else
static inline int      nativeSock(int s) { return s; }
static inline void     closeSocket(int s) { close(s); }
static inline bool     isTimeout()   { return errno == EAGAIN || errno == EWOULDBLOCK; }
static inline bool     isInterrupt() { return errno == EINTR; }
#endif

// ─────────────────────────────────────────────────────────────────────────────

IPCBridge::~IPCBridge() { stop(); }

bool IPCBridge::start()
{
    if (m_running.load()) return true;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return false;
#endif

    // Create UDP socket
#ifdef _WIN32
    SOCKET rawSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rawSock == INVALID_SOCKET) { WSACleanup(); return false; }
    m_sock = static_cast<socket_t>(rawSock);
#else
    int rawSock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rawSock < 0) return false;
    m_sock = rawSock;
#endif

    // 100 ms receive timeout so the loop can check m_running without blocking forever.
#ifdef _WIN32
    DWORD tvMs = 100;
    setsockopt(nativeSock(m_sock), SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&tvMs), sizeof(tvMs));
#else
    timeval tv{0, 100'000};
    setsockopt(m_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Bind to loopback:PLUGIN_PORT
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(PLUGIN_PORT));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(nativeSock(m_sock), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket(m_sock);
        m_sock = kInvalid;
#ifdef _WIN32
        WSACleanup();
#endif
        return false;
    }

    m_running.store(true);
    m_thread = std::thread(&IPCBridge::receiveLoop, this);
    return true;
}

void IPCBridge::stop()
{
    if (!m_running.exchange(false)) return;

    // Closing the socket unblocks any pending recvfrom immediately.
    if (m_sock != kInvalid) {
        closeSocket(m_sock);
        m_sock = kInvalid;
    }

    if (m_thread.joinable())
        m_thread.join();

#ifdef _WIN32
    WSACleanup();
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Receive loop — background thread
// ─────────────────────────────────────────────────────────────────────────────
void IPCBridge::receiveLoop()
{
    char buf[256];

    while (m_running.load()) {
        if (m_sock == kInvalid) break;

        sockaddr_in from{};
#ifdef _WIN32
        int fromLen = sizeof(from);
        int n = recvfrom(nativeSock(m_sock), buf, static_cast<int>(sizeof(buf) - 1),
                         0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n == SOCKET_ERROR) {
            if (isTimeout()) continue;
            break;  // socket closed or unrecoverable error
        }
#else
        socklen_t fromLen = sizeof(from);
        ssize_t   n       = recvfrom(m_sock, buf, sizeof(buf) - 1,
                                     0, reinterpret_cast<sockaddr*>(&from), &fromLen);
        if (n < 0) {
            if (isTimeout() || isInterrupt()) continue;
            break;
        }
#endif

        buf[n] = '\0';

        // Strip trailing whitespace / CR / LF
        while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' || buf[n-1] == ' '))
            buf[--n] = '\0';

        if (std::strncmp(buf, "POS_BEATS ", 10) == 0) {
            double beats = std::strtod(buf + 10, nullptr);
            if (m_onPosition) m_onPosition(beats);

        } else if (std::strncmp(buf, "TEMPO_BPM ", 10) == 0) {
            double bpm = std::strtod(buf + 10, nullptr);
            if (m_onTempo) m_onTempo(bpm);

        } else if (std::strcmp(buf, "PLAYING") == 0) {
            if (m_onPlayState) m_onPlayState(true);

        } else if (std::strcmp(buf, "STOPPED") == 0) {
            if (m_onPlayState) m_onPlayState(false);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Senders — callable from audio or GUI thread
// ─────────────────────────────────────────────────────────────────────────────
void IPCBridge::sendPacket(const char* msg)
{
    if (m_sock == kInvalid) return;

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(static_cast<uint16_t>(CTRL_PORT));
    dest.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    auto len = static_cast<int>(std::strlen(msg));
    sendto(nativeSock(m_sock), msg, len, 0,
           reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

void IPCBridge::sendSeekBeats(double beats)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "SEEK_BEATS %.6f\n", beats);
    sendPacket(buf);
}

void IPCBridge::sendPlay() { sendPacket("PLAY\n"); }
void IPCBridge::sendStop() { sendPacket("STOP\n"); }
