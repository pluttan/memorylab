// The network card through the UEFI Simple Network Protocol: DHCP, ARP and UDP.
#ifndef LABOS_NET_H
#define LABOS_NET_H

#include "netpkt.h"

typedef void (*udp_handler)(const udp_view *datagram);

// Takes the first network card for itself and starts DHCP; 0 when there is no card
int net_init(void);

// Handles every received frame and drives DHCP; call it often from the main loop
void net_poll(void);

void net_listen(u16 port, udp_handler handler);
int net_send_udp(const u8 dst_mac[6], u32 dst_ip, u16 src_port, u16 dst_port, const void *data, usize length);

// "10.0.2.15", or a state such as "DHCP…" until an address is known
void net_status(char *out, usize size);
u32 net_ip(void);

#endif
