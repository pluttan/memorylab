#include "remote.h"

#include "gfx.h"
#include "lib.h"
#include "net.h"

#define CHUNK 1400
#define WINDOW 64
#define BLOB_PAGES 2048  // 8 MB: an uncompressed 1920x1080 screen still fits
#define MAX_COMMANDS 16

int remote_reset_requested;

static u8 *blob;
static usize blob_length;
static u32 blob_id = 0xffffffff;
static struct {
    const char *name;
    remote_command command;
} commands[MAX_COMMANDS];

// Where the last request came from: replies go straight back to that MAC, router or not
static u8 peer_mac[6];
static u32 peer_ip;
static u16 peer_port;

static usize chunk_count(void) { return blob_length ? (blob_length + CHUNK - 1) / CHUNK : 1; }

static void send_chunks(usize first, usize count) {
    u8 datagram[CHUNK + 48];
    usize total = chunk_count();
    for (usize i = first; i < first + count && i < total; i++) {
        usize head = fmt((char *)datagram, 48, "%u %lu %lu\n", blob_id, i, total);
        usize offset = i * CHUNK;
        usize n = blob_length - offset < CHUNK ? blob_length - offset : CHUNK;
        if (!blob_length) n = 0;
        memcpy(datagram + head, blob + offset, n);
        net_send_udp(peer_mac, peer_ip, REMOTE_PORT, peer_port, datagram, head + n);
    }
}

static const char *skip_word(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

static u64 parse_number(const char **s) {
    u64 v = 0;
    while (**s >= '0' && **s <= '9') v = v * 10 + (u64)(*(*s)++ - '0');
    while (**s == ' ') (*s)++;
    return v;
}

static int word_is(const char *s, const char *word) {
    usize n = strlen(word);
    return !strncmp(s, word, n) && (s[n] == 0 || s[n] == ' ');
}

static void on_datagram(const udp_view *v) {
    char line[512];
    usize n = v->length < sizeof line - 1 ? v->length : sizeof line - 1;
    memcpy(line, v->payload, n);
    while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) n--;
    line[n] = 0;
    memcpy(peer_mac, v->src_mac, 6);
    peer_ip = v->src_ip;
    peer_port = v->src_port;

    const char *p = line;
    if (word_is(p, "more")) {
        p = skip_word(p);
        u32 id = (u32)parse_number(&p);
        usize first = parse_number(&p), count = parse_number(&p);
        if (id == blob_id) send_chunks(first, count);
        return;
    }
    u32 id = (u32)parse_number(&p);
    if (id == blob_id) {
        send_chunks(0, WINDOW);
        return;
    }
    const char *args = skip_word(p);
    int found = 0;
    for (int i = 0; i < MAX_COMMANDS && commands[i].command && !found; i++) {
        if (word_is(p, commands[i].name)) {
            blob_length = commands[i].command(args, blob, BLOB_PAGES * 4096);
            found = 1;
        }
    }
    if (!found) blob_length = fmt((char *)blob, 256, "error: unknown command '%s'", line);
    blob_id = id;
    send_chunks(0, WINDOW);
}

void remote_register(const char *name, remote_command command) {
    for (int i = 0; i < MAX_COMMANDS; i++) {
        if (!commands[i].command) {
            commands[i].name = name;
            commands[i].command = command;
            return;
        }
    }
}

// ==========================
// ===  Built-in commands ===
// ==========================

static usize cmd_ping(const char *args, u8 *out, usize cap) {
    (void)args;
    char ip[32];
    net_status(ip, sizeof ip);
    return fmt((char *)out, cap, "labos %ux%u %s tsc %.3f GHz", screen.width, screen.height, ip, tsc_per_ns);
}

static usize cmd_log(const char *args, u8 *out, usize cap) {
    (void)args;
    usize kept = log_head < LOG_RING_SIZE ? log_head : LOG_RING_SIZE;
    if (kept > cap) kept = cap;
    for (usize i = 0; i < kept; i++) out[i] = (u8)log_ring[(log_head - kept + i) % LOG_RING_SIZE];
    return kept;
}

static usize cmd_screen(const char *args, u8 *out, usize cap) {
    (void)args;
    usize head = fmt((char *)out, 64, "rle %u %u\n", screen.width, screen.height);
    usize body = rle_encode(screen.pixels, (usize)screen.width * screen.height, out + head, cap - head);
    return body ? head + body : fmt((char *)out, cap, "error: the screen does not fit the reply buffer");
}

static usize cmd_reset(const char *args, u8 *out, usize cap) {
    (void)args;
    remote_reset_requested = 1;
    return fmt((char *)out, cap, "ok");
}

int remote_init(void) {
    blob = alloc_pages(BLOB_PAGES);
    if (!blob) return 0;
    remote_register("ping", cmd_ping);
    remote_register("log", cmd_log);
    remote_register("screen", cmd_screen);
    remote_register("reset", cmd_reset);
    net_listen(REMOTE_PORT, on_datagram);
    return 1;
}
