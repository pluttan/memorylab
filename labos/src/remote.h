// Commands from tools/labctl.py over UDP.
//
// Request: "<id> <command> [arguments]". Reply datagrams: "<id> <index> <total>\n" and a chunk of the
// reply. The first 64 chunks go at once; "more <id> <first> <count>" asks for the rest or the lost ones.
// A repeated request with the same id is answered from the stored reply without running it again.
#ifndef LABOS_REMOTE_H
#define LABOS_REMOTE_H

#include "efi.h"

#define REMOTE_PORT 5555

// Writes the reply into out (at most cap bytes) and returns its length
typedef usize (*remote_command)(const char *args, u8 *out, usize cap);

int remote_init(void);
void remote_register(const char *name, remote_command command);

// Set by the "reset" command; the main loop restarts the machine after the reply has left
extern int remote_reset_requested;

#endif
