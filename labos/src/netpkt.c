#include "netpkt.h"

static const u8 BROADCAST_MAC[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8), p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24), p[1] = (u8)(v >> 16), p[2] = (u8)(v >> 8), p[3] = (u8)v; }
static u16 get16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 get32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static void copy(u8 *dst, const void *src, usize n) {
    const u8 *s = src;
    while (n--) *dst++ = *s++;
}

u16 ip_checksum(const void *data, usize length) {
    const u8 *p = data;
    u32 sum = 0;
    for (; length > 1; p += 2, length -= 2) sum += get16(p);
    if (length) sum += (u32)p[0] << 8;
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (u16)~sum;
}

usize build_udp(u8 *frame, const u8 src_mac[6], const u8 dst_mac[6], u32 src_ip, u32 dst_ip, u16 src_port,
                u16 dst_port, const void *payload, usize length) {
    copy(frame, dst_mac, 6);
    copy(frame + 6, src_mac, 6);
    put16(frame + 12, ETHERTYPE_IP);
    u8 *ip = frame + ETH_HEADER;
    ip[0] = 0x45;  // IPv4, 20 byte header
    ip[1] = 0;
    put16(ip + 2, (u16)(IP_HEADER + UDP_HEADER + length));
    put16(ip + 4, 0);
    put16(ip + 6, 0x4000);  // don't fragment
    ip[8] = 64;
    ip[9] = 17;  // UDP
    put16(ip + 10, 0);
    put32(ip + 12, src_ip);
    put32(ip + 16, dst_ip);
    put16(ip + 10, ip_checksum(ip, IP_HEADER));
    u8 *udp = ip + IP_HEADER;
    put16(udp, src_port);
    put16(udp + 2, dst_port);
    put16(udp + 4, (u16)(UDP_HEADER + length));
    put16(udp + 6, 0);  // the UDP checksum is optional over IPv4
    copy(udp + UDP_HEADER, payload, length);
    return ETH_HEADER + IP_HEADER + UDP_HEADER + length;
}

int parse_udp(const u8 *frame, usize length, udp_view *out) {
    if (length < ETH_HEADER + IP_HEADER + UDP_HEADER || get16(frame + 12) != ETHERTYPE_IP) return 0;
    const u8 *ip = frame + ETH_HEADER;
    usize ihl = (usize)(ip[0] & 0x0f) * 4;
    if ((ip[0] >> 4) != 4 || ihl < IP_HEADER || ip[9] != 17) return 0;
    if ((get16(ip + 6) & 0x3fff) != 0) return 0;  // fragments are not reassembled
    usize total = get16(ip + 2);
    if (total > length - ETH_HEADER || total < ihl + UDP_HEADER || ip_checksum(ip, ihl) != 0) return 0;
    const u8 *udp = ip + ihl;
    usize udp_length = get16(udp + 4);
    if (udp_length < UDP_HEADER || udp_length > total - ihl) return 0;
    copy(out->src_mac, frame + 6, 6);
    out->src_ip = get32(ip + 12);
    out->dst_ip = get32(ip + 16);
    out->src_port = get16(udp);
    out->dst_port = get16(udp + 2);
    out->payload = udp + UDP_HEADER;
    out->length = udp_length - UDP_HEADER;
    return 1;
}

usize build_arp_reply(u8 *out, const u8 *frame, usize length, const u8 my_mac[6], u32 my_ip) {
    if (length < ETH_HEADER + 28 || get16(frame + 12) != ETHERTYPE_ARP) return 0;
    const u8 *arp = frame + ETH_HEADER;
    // Ethernet + IPv4 request for our address
    if (get16(arp) != 1 || get16(arp + 2) != ETHERTYPE_IP || get16(arp + 6) != 1) return 0;
    if (!my_ip || get32(arp + 24) != my_ip) return 0;
    copy(out, arp + 8, 6);
    copy(out + 6, my_mac, 6);
    put16(out + 12, ETHERTYPE_ARP);
    u8 *r = out + ETH_HEADER;
    put16(r, 1);
    put16(r + 2, ETHERTYPE_IP);
    r[4] = 6, r[5] = 4;
    put16(r + 6, 2);  // reply
    copy(r + 8, my_mac, 6);
    put32(r + 14, my_ip);
    copy(r + 18, arp + 8, 10);  // sender MAC and IP of the request become the target
    return ETH_HEADER + 28;
}

// ==========================
// ===  DHCP              ===
// ==========================

#define DHCP_MAGIC 0x63825363
#define DHCP_FIXED 236

usize dhcp_build(u8 *p, int type, u32 xid, const u8 mac[6], u32 requested_ip, u32 server_id) {
    for (usize i = 0; i < DHCP_FIXED + 64; i++) p[i] = 0;
    p[0] = 1;  // request
    p[1] = 1;  // Ethernet
    p[2] = 6;
    put32(p + 4, xid);
    put16(p + 10, 0x8000);  // ask the server to broadcast: we have no address yet
    copy(p + 28, mac, 6);
    put32(p + DHCP_FIXED, DHCP_MAGIC);
    u8 *o = p + DHCP_FIXED + 4;
    *o++ = 53, *o++ = 1, *o++ = (u8)type;
    if (type == DHCP_REQUEST) {
        *o++ = 50, *o++ = 4, put32(o, requested_ip), o += 4;
        *o++ = 54, *o++ = 4, put32(o, server_id), o += 4;
    }
    *o++ = 55, *o++ = 2, *o++ = 1, *o++ = 3;  // ask for the netmask and the router
    *o++ = 255;
    return (usize)(o - p);
}

int dhcp_parse(const u8 *p, usize length, u32 xid, dhcp_reply *out) {
    if (length < DHCP_FIXED + 4 || p[0] != 2 || get32(p + 4) != xid || get32(p + DHCP_FIXED) != DHCP_MAGIC) return 0;
    out->type = 0;
    out->your_ip = get32(p + 16);
    out->server_id = out->netmask = out->router = 0;
    for (usize i = DHCP_FIXED + 4; i < length && p[i] != 255;) {
        u8 code = p[i];
        if (code == 0) {
            i++;
            continue;
        }
        if (i + 1 >= length || i + 2 + p[i + 1] > length) return 0;
        const u8 *v = p + i + 2;
        u8 n = p[i + 1];
        if (code == 53 && n >= 1) out->type = v[0];
        if (code == 54 && n >= 4) out->server_id = get32(v);
        if (code == 1 && n >= 4) out->netmask = get32(v);
        if (code == 3 && n >= 4) out->router = get32(v);
        i += 2 + n;
    }
    return out->type != 0;
}

// ==========================
// ===  Screen snapshot   ===
// ==========================

usize rle_encode(const u32 *pixels, usize count, u8 *out, usize cap) {
    usize size = 0;
    for (usize i = 0; i < count;) {
        u32 c = pixels[i] & 0xffffff;
        usize run = 1;
        while (i + run < count && run < 0xffff && (pixels[i + run] & 0xffffff) == c) run++;
        if (size + 5 > cap) return 0;
        out[size++] = (u8)run, out[size++] = (u8)(run >> 8);
        out[size++] = (u8)c, out[size++] = (u8)(c >> 8), out[size++] = (u8)(c >> 16);
        i += run;
    }
    return size;
}

const u8 *net_broadcast_mac(void) { return BROADCAST_MAC; }
