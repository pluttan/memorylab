// Host tests of packet building and parsing.
#include <stdio.h>
#include <string.h>

#include "netpkt.h"

static int failures;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                            \
        }                                                          \
    } while (0)

static const u8 MAC_A[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const u8 MAC_B[6] = {0x52, 0x55, 0x0a, 0x00, 0x02, 0x02};
#define IP_A 0x0a00020f  // 10.0.2.15
#define IP_B 0x0a000202  // 10.0.2.2

static void test_checksum(void) {
    // The example header from RFC 1071 style checks: a header with its checksum sums to zero
    u8 header[20] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                     0x00, 0x00, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    u16 sum = ip_checksum(header, 20);
    CHECK(sum == 0xb861);
    header[10] = (u8)(sum >> 8), header[11] = (u8)sum;
    CHECK(ip_checksum(header, 20) == 0);
}

static void test_udp_roundtrip(void) {
    u8 frame[FRAME_MAX];
    usize n = build_udp(frame, MAC_A, MAC_B, IP_A, IP_B, 5555, 40000, "pong", 4);
    CHECK(n == 14 + 20 + 8 + 4);
    udp_view v;
    CHECK(parse_udp(frame, n, &v));
    CHECK(!memcmp(v.src_mac, MAC_A, 6));
    CHECK(v.src_ip == IP_A && v.dst_ip == IP_B);
    CHECK(v.src_port == 5555 && v.dst_port == 40000);
    CHECK(v.length == 4 && !memcmp(v.payload, "pong", 4));
    frame[14 + 12] ^= 1;  // a damaged header is dropped
    CHECK(!parse_udp(frame, n, &v));
    CHECK(!parse_udp(frame, 30, &v));
}

static void test_arp(void) {
    u8 req[60] = {0}, out[64];
    memset(req, 0xff, 6);
    memcpy(req + 6, MAC_B, 6);
    req[12] = 0x08, req[13] = 0x06;
    u8 *a = req + 14;
    a[1] = 1, a[2] = 0x08, a[4] = 6, a[5] = 4, a[7] = 1;
    memcpy(a + 8, MAC_B, 6);
    a[14] = 10, a[15] = 0, a[16] = 2, a[17] = 2;
    a[24] = 10, a[25] = 0, a[26] = 2, a[27] = 15;
    usize n = build_arp_reply(out, req, sizeof req, MAC_A, IP_A);
    CHECK(n == 42);
    CHECK(!memcmp(out, MAC_B, 6) && !memcmp(out + 6, MAC_A, 6));
    CHECK(out[14 + 7] == 2);
    CHECK(!memcmp(out + 14 + 8, MAC_A, 6));
    CHECK(out[14 + 14] == 10 && out[14 + 17] == 15);
    CHECK(!memcmp(out + 14 + 18, MAC_B, 6) && out[14 + 27] == 2);
    CHECK(build_arp_reply(out, req, sizeof req, MAC_A, IP_B) == 0);  // not our address
}

static void test_dhcp(void) {
    u8 p[600];
    usize n = dhcp_build(p, DHCP_DISCOVER, 0x1234abcd, MAC_A, 0, 0);
    CHECK(n > 240 && p[0] == 1 && p[240 + 2] == DHCP_DISCOVER);  // options follow the 236 + 4 byte head
    dhcp_reply r;
    CHECK(!dhcp_parse(p, n, 0x1234abcd, &r));  // our own request is not a reply
    // Turn it into an OFFER from 10.0.2.2 with a netmask and a router
    p[0] = 2;
    p[16] = 10, p[17] = 0, p[18] = 2, p[19] = 15;
    u8 *o = p + 240;
    *o++ = 53, *o++ = 1, *o++ = DHCP_OFFER;
    *o++ = 54, *o++ = 4, *o++ = 10, *o++ = 0, *o++ = 2, *o++ = 2;
    *o++ = 1, *o++ = 4, *o++ = 255, *o++ = 255, *o++ = 255, *o++ = 0;
    *o++ = 3, *o++ = 4, *o++ = 10, *o++ = 0, *o++ = 2, *o++ = 2;
    *o++ = 255;
    n = (usize)(o - p);
    CHECK(dhcp_parse(p, n, 0x1234abcd, &r));
    CHECK(r.type == DHCP_OFFER && r.your_ip == IP_A && r.server_id == IP_B);
    CHECK(r.netmask == 0xffffff00 && r.router == IP_B);
    CHECK(!dhcp_parse(p, n, 0x1234abce, &r));  // another transaction
    p[241] = 200;  // an option longer than the packet
    CHECK(!dhcp_parse(p, n, 0x1234abcd, &r));
}

static void test_rle(void) {
    u32 px[] = {0x171717, 0x171717, 0x171717, 0xf38ba8, 0x040404, 0x040404};
    u8 out[64];
    usize n = rle_encode(px, 6, out, sizeof out);
    CHECK(n == 15);
    CHECK(out[0] == 3 && out[1] == 0 && out[2] == 0x17);
    CHECK(out[5] == 1 && out[7] == 0xa8 && out[8] == 0x8b && out[9] == 0xf3);
    CHECK(out[10] == 2 && out[12] == 0x04);
    CHECK(rle_encode(px, 6, out, 10) == 0);
}

int main(void) {
    test_checksum();
    test_udp_roundtrip();
    test_arp();
    test_dhcp();
    test_rle();
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
