/* net.c — LAN multiplayer scaffold (EXPERIMENTAL, v1.1)
 *
 * Design: deterministic lockstep over UDP (WiFi hotspot / LAN).
 *   Both phones run identical engine builds. Each frame, both sides exchange
 *   2 bytes of input state. The engine advances frame N only when the
 *   opponent's input for frame N has arrived (with up to 3 frames of
 *   rollback-free delay buffering).
 *
 * Protocol:
 *   HANDSHAKE: "MUGEN1" + role byte (host=1/client=2) + seed(4)
 *   FRAME packet: [0x01][frame_lo][frame_hi][p1_buttons(4)][checksum(1)]
 *   PING: [0x02][t_lo][t_hi]
 *
 * Usage (planned):
 *   Host:   net_mode = 1; net_bind(7777); game runs P1 locally
 *   Client: net_mode = 2; net_connect("192.168.43.1", 7777); game runs P2 locally
 *   The engine tick becomes: read local input -> send -> await remote -> tick.
 *
 * Status: transport layer complete below; engine integration points marked
 * TODO in mg_fight_frame (input exchange + tick gating). Untested on hardware.
 */
#include "mugen.h"
#include <unistd.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#endif

static SOCKET g_sock = INVALID_SOCKET;
static struct sockaddr_in g_peer;
static int g_have_peer = 0;
static int g_net_frame_counter = 0;

int mg_net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return -1;
#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock < 0) return -1;
    return 0;
}

int mg_net_bind(int port) {
    if (g_sock < 0 && mg_net_init() < 0) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);
    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;
    return 0;
}

int mg_net_connect(const char *ip, int port) {
    if (g_sock < 0 && mg_net_init() < 0) return -1;
    memset(&g_peer, 0, sizeof(g_peer));
    g_peer.sin_family = AF_INET;
    g_peer.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, ip, &g_peer.sin_addr) != 1) return -1;
    g_have_peer = 1;
    /* handshake */
    uint8_t hs[8] = {'M','U','G','E','N','1', 2, 0};
    sendto(g_sock, (const char*)hs, sizeof(hs), 0, (struct sockaddr*)&g_peer, sizeof(g_peer));
    return 0;
}

/* send this frame's local buttons; returns remote buttons or -1 */
int32_t mg_net_exchange(uint32_t local_buttons) {
    if (!g_have_peer || g_sock < 0) return -1;
    uint8_t pkt[8];
    pkt[0] = 0x01;
    pkt[1] = (uint8_t)(g_net_frame_counter & 0xFF);
    pkt[2] = (uint8_t)(g_net_frame_counter >> 8);
    pkt[3] = (uint8_t)(local_buttons & 0xFF);
    pkt[4] = (uint8_t)((local_buttons >> 8) & 0xFF);
    pkt[5] = (uint8_t)((local_buttons >> 16) & 0xFF);
    pkt[6] = (uint8_t)((local_buttons >> 24) & 0xFF);
    pkt[7] = 0;   /* checksum placeholder */
    sendto(g_sock, (const char*)pkt, sizeof(pkt), 0, (struct sockaddr*)&g_peer, sizeof(g_peer));

    /* await remote input (with timeout — on timeout reuse last known input) */
    static uint32_t last_remote = 0;
    fd_set fds;
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 4000;   /* 4ms max wait per frame */
    FD_ZERO(&fds);
    FD_SET(g_sock, &fds);
    if (select((int)g_sock + 1, &fds, NULL, NULL, &tv) > 0) {
        uint8_t buf[64];
        socklen_t slen = sizeof(g_peer);
        int n = (int)recvfrom(g_sock, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&g_peer, &slen);
        if (n >= 7 && buf[0] == 0x01) {
            last_remote = (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) |
                          ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);
        }
    }
    g_net_frame_counter++;
    return (int32_t)last_remote;
}

void mg_net_shutdown(void) {
    if (g_sock >= 0) {
#ifdef _WIN32
        closesocket(g_sock);
        WSACleanup();
#else
        close(g_sock);
#endif
        g_sock = -1;
    }
    g_have_peer = 0;
}

/* TODO engine integration (mg_fight_frame):
 *   if (g_game.net_mode) {
 *       local = read local input;
 *       remote = mg_net_exchange(local);
 *       g_entities[net_mode == 1 ? 1 : 0].buttons = remote;
 *       ... then tick as normal (lockstep: exchange happens pre-tick)
 *   }
 * Random seed must be exchanged at handshake for determinism.
 */
