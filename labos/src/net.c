#include "net.h"

#include "lib.h"

#define MAX_LISTENERS 4
#define DHCP_CLIENT_PORT 68
#define DHCP_SERVER_PORT 67
#define DHCP_RETRY_US 2000000

typedef enum { NET_DOWN, NET_DISCOVER, NET_REQUEST, NET_UP } net_state;

static efi_snp *snp;
static u8 mac[6];
static u32 my_ip, server_id, offered_ip;
static u32 xid;
static net_state state = NET_DOWN;
static u64 last_dhcp_us;
static struct {
    u16 port;
    udp_handler handler;
} listeners[MAX_LISTENERS];

// The firmware hands the transmit buffer back through GetStatus; until then it must not change
static u8 tx_frame[FRAME_MAX];
static u8 rx_frame[FRAME_MAX + 64];

static int transmit(usize length) {
    if (length < 60) {
        memset(tx_frame + length, 0, 60 - length);
        length = 60;
    }
    if (snp->transmit(snp, 0, length, tx_frame, 0, 0, 0) != EFI_SUCCESS) return 0;
    for (int i = 0; i < 100000; i++) {
        u32 irq = 0;
        void *done = 0;
        if (snp->get_status(snp, &irq, &done) != EFI_SUCCESS) return 0;
        if (done) return 1;
    }
    log_line("net: transmit buffer not returned");
    return 0;
}

int net_send_udp(const u8 dst_mac[6], u32 dst_ip, u16 src_port, u16 dst_port, const void *data, usize length) {
    if (!snp || length > UDP_PAYLOAD_MAX) return 0;
    return transmit(build_udp(tx_frame, mac, dst_mac, my_ip, dst_ip, src_port, dst_port, data, length));
}

void net_listen(u16 port, udp_handler handler) {
    for (int i = 0; i < MAX_LISTENERS; i++) {
        if (!listeners[i].handler) {
            listeners[i].port = port;
            listeners[i].handler = handler;
            return;
        }
    }
}

// ==========================
// ===  Card selection    ===
// ==========================

static efi_snp *open_card(void) {
    efi_guid guid = EFI_SNP_GUID;
    efi_handle *handles = 0;
    usize count = 0;
    if (BS->locate_handle_buffer(EFI_BY_PROTOCOL, &guid, 0, &count, &handles) != EFI_SUCCESS) return 0;
    efi_snp *found = 0;
    for (usize i = 0; i < count && !found; i++) {
        efi_snp *candidate = 0;
        // Exclusive access disconnects the firmware network stack, which would otherwise eat our frames.
        // BY_DRIVER would need a controller handle; an application asks for EXCLUSIVE alone
        efi_status s = BS->open_protocol(handles[i], &guid, (void **)&candidate, IMAGE, 0, EFI_OPEN_PROTOCOL_EXCLUSIVE);
        if (s != EFI_SUCCESS) {
            log_line("net: card %lu: exclusive open failed (%lx), sharing it", i, s);
            s = BS->handle_protocol(handles[i], &guid, (void **)&candidate);
        }
        if (s == EFI_SUCCESS && candidate && candidate->mode->hw_address_size == 6) found = candidate;
    }
    BS->free_pool(handles);
    log_line("net: %lu card(s), %s", count, found ? "using the first" : "none usable");
    return found;
}

int net_init(void) {
    snp = open_card();
    if (!snp) return 0;
    if (snp->mode->state == EFI_SNP_STOPPED) snp->start(snp);
    if (snp->mode->state == EFI_SNP_STARTED) snp->initialize(snp, 0, 0);
    if (snp->mode->state != EFI_SNP_INITIALIZED) {
        log_line("net: the card did not initialize (state %u)", snp->mode->state);
        snp = 0;
        return 0;
    }
    snp->receive_filters(snp, EFI_SNP_RECEIVE_UNICAST | EFI_SNP_RECEIVE_BROADCAST, 0, 0, 0, 0);
    memcpy(mac, snp->mode->current_address.addr, 6);
    log_line("net: MAC %02x:%02x:%02x:%02x:%02x:%02x, link %s", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
             snp->mode->media_present_supported && !snp->mode->media_present ? "down" : "up");
    xid = (u32)rdtsc();
    state = NET_DISCOVER;
    return 1;
}

// ==========================
// ===  DHCP              ===
// ==========================

static void dhcp_send(int type) {
    u8 payload[600];
    usize n = dhcp_build(payload, type, xid, mac, offered_ip, server_id);
    // No address yet: from 0.0.0.0 to the broadcast address
    transmit(build_udp(tx_frame, mac, net_broadcast_mac(), 0, 0xffffffff, DHCP_CLIENT_PORT, DHCP_SERVER_PORT, payload,
                       n));
    last_dhcp_us = now_us();
}

static void dhcp_tick(void) {
    if ((state == NET_DISCOVER || state == NET_REQUEST) && (!last_dhcp_us || now_us() - last_dhcp_us > DHCP_RETRY_US)) {
        // A lost OFFER or ACK: start over
        state = NET_DISCOVER;
        dhcp_send(DHCP_DISCOVER);
    }
}

static void dhcp_receive(const udp_view *v) {
    dhcp_reply r;
    if (!dhcp_parse(v->payload, v->length, xid, &r)) return;
    if (state == NET_DISCOVER && r.type == DHCP_OFFER) {
        offered_ip = r.your_ip;
        server_id = r.server_id ? r.server_id : v->src_ip;
        state = NET_REQUEST;
        dhcp_send(DHCP_REQUEST);
    } else if (state == NET_REQUEST && r.type == DHCP_ACK) {
        my_ip = r.your_ip;
        state = NET_UP;
        log_line("net: address %u.%u.%u.%u", my_ip >> 24, (my_ip >> 16) & 255, (my_ip >> 8) & 255, my_ip & 255);
    } else if (r.type == DHCP_NAK) {
        state = NET_DISCOVER;
        last_dhcp_us = 0;
    }
}

// ==========================
// ===  Receive           ===
// ==========================

void net_poll(void) {
    if (!snp) return;
    dhcp_tick();
    for (int budget = 0; budget < 64; budget++) {
        usize length = sizeof rx_frame;
        efi_status s = snp->receive(snp, 0, &length, rx_frame, 0, 0, 0);
        if (s == EFI_NOT_READY) return;
        if (s != EFI_SUCCESS) continue;
        usize reply = build_arp_reply(tx_frame, rx_frame, length, mac, my_ip);
        if (reply) {
            transmit(reply);
            continue;
        }
        udp_view v;
        if (!parse_udp(rx_frame, length, &v)) continue;
        if (v.dst_port == DHCP_CLIENT_PORT) {
            dhcp_receive(&v);
            continue;
        }
        if (state != NET_UP || v.dst_ip != my_ip) continue;
        for (int i = 0; i < MAX_LISTENERS; i++) {
            if (listeners[i].handler && listeners[i].port == v.dst_port) listeners[i].handler(&v);
        }
    }
}

u32 net_ip(void) { return state == NET_UP ? my_ip : 0; }

void net_status(char *out, usize size) {
    if (!snp) {
        fmt(out, size, "нет сети");
    } else if (state != NET_UP) {
        fmt(out, size, "DHCP…");
    } else {
        fmt(out, size, "%u.%u.%u.%u", my_ip >> 24, (my_ip >> 16) & 255, (my_ip >> 8) & 255, my_ip & 255);
    }
}
