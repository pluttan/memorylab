// Building and parsing Ethernet, ARP, IPv4, UDP and DHCP packets. Pure: tests run it on the host.
// IPv4 addresses are u32 in host order: 10.0.2.15 is 0x0a00020f.
#ifndef LABOS_NETPKT_H
#define LABOS_NETPKT_H

#include "efi.h"

#define ETH_HEADER 14
#define IP_HEADER 20
#define UDP_HEADER 8
#define UDP_PAYLOAD_MAX 1472  // 1500 byte Ethernet payload minus the IP and UDP headers
#define FRAME_MAX 1514

#define ETHERTYPE_IP 0x0800
#define ETHERTYPE_ARP 0x0806

typedef struct {
    u8 src_mac[6];
    u32 src_ip, dst_ip;
    u16 src_port, dst_port;
    const u8 *payload;
    usize length;
} udp_view;

u16 ip_checksum(const void *data, usize length);

// Ethernet + IPv4 + UDP around the payload; returns the frame length
usize build_udp(u8 *frame, const u8 src_mac[6], const u8 dst_mac[6], u32 src_ip, u32 dst_ip, u16 src_port,
                u16 dst_port, const void *payload, usize length);

const u8 *net_broadcast_mac(void);

// 1 when the frame is an unfragmented IPv4 UDP datagram with a valid header checksum
int parse_udp(const u8 *frame, usize length, udp_view *out);

// The reply to an ARP request for my_ip, or 0 when the frame is something else
usize build_arp_reply(u8 *out, const u8 *frame, usize length, const u8 my_mac[6], u32 my_ip);

// ==========================
// ===  DHCP              ===
// ==========================

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_ACK 5
#define DHCP_NAK 6

typedef struct {
    int type;
    u32 your_ip, server_id, netmask, router;
} dhcp_reply;

// A DISCOVER or a REQUEST (requested_ip and server_id are used by REQUEST only)
usize dhcp_build(u8 *payload, int type, u32 xid, const u8 mac[6], u32 requested_ip, u32 server_id);

// 1 when the payload is a DHCP reply to our xid
int dhcp_parse(const u8 *payload, usize length, u32 xid, dhcp_reply *out);

// ==========================
// ===  Screen snapshot   ===
// ==========================

// Runs of equal pixels: u16 count (little endian), then B, G, R. Returns the size, or 0 if cap is too small
usize rle_encode(const u32 *pixels, usize count, u8 *out, usize cap);

#endif
