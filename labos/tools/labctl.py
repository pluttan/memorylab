"""Control labos over UDP.

Usage: python3.12 tools/labctl.py [--host HOST] [--port PORT] [--timeout S] COMMAND [ARGS...]
  ping              address, screen size, TSC frequency
  log               the log ring of labos
  screen OUT.png    the current screen
  reset             restart the machine
  anything else     sent as is, the reply is printed
"""
import argparse
import random
import socket
import sys
import time

WINDOW = 64


def runs(indices):
    """Contiguous runs of sorted indices as (first, count)."""
    out = []
    for i in indices:
        if out and out[-1][0] + out[-1][1] == i and out[-1][1] < WINDOW:
            out[-1][1] += 1
        else:
            out.append([i, 1])
    return out


def request(host, port, line, timeout):
    """Send one command and gather every chunk of the reply."""
    req_id = random.randrange(1, 2**31)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(0.2)
    message = f'{req_id} {line}'.encode()
    sock.sendto(message, (host, port))
    chunks, total = {}, None
    deadline = time.monotonic() + timeout
    while total is None or len(chunks) < total:
        try:
            data, _ = sock.recvfrom(65536)
        except socket.timeout:
            if time.monotonic() > deadline:
                raise TimeoutError(f'no reply to "{line}" from {host}:{port}')
            if total is None:
                # The request or the whole first window is lost; the same id is never run twice
                sock.sendto(message, (host, port))
            else:
                missing = [i for i in range(total) if i not in chunks]
                for first, count in runs(missing)[:4]:
                    sock.sendto(f'more {req_id} {first} {count}'.encode(), (host, port))
            continue
        head, _, body = data.partition(b'\n')
        try:
            rid, index, count = (int(x) for x in head.split())
        except ValueError:
            continue
        if rid != req_id:
            continue
        total = count
        chunks[index] = body
        deadline = time.monotonic() + timeout
        if index % WINDOW == WINDOW - 1 and index + 1 < total:
            # The window arrived: ask for the next one without waiting for a timeout
            sock.sendto(f'more {req_id} {index + 1} {WINDOW}'.encode(), (host, port))
    return b''.join(chunks[i] for i in range(total))


def save_screen(blob, path):
    from PIL import Image

    head, _, body = blob.partition(b'\n')
    kind, width, height = head.split()
    if kind != b'rle':
        sys.exit(blob.decode(errors='replace'))
    width, height = int(width), int(height)
    pixels = bytearray()
    for i in range(0, len(body), 5):
        run = body[i] | body[i + 1] << 8
        b, g, r = body[i + 2], body[i + 3], body[i + 4]
        pixels += bytes((r, g, b)) * run
    Image.frombytes('RGB', (width, height), bytes(pixels)).save(path)
    print(f'{path}: {width}x{height}, {len(blob)} bytes over the network')


def main():
    parser = argparse.ArgumentParser(description='Control labos over UDP')
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=5555)
    parser.add_argument('--timeout', type=float, default=5.0, help='seconds without any reply')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if not args.command:
        parser.error('a command is required')
    name, rest = args.command[0], args.command[1:]
    if name == 'screen':
        if len(rest) != 1:
            parser.error('screen OUT.png')
        save_screen(request(args.host, args.port, 'screen', args.timeout), rest[0])
        return
    reply = request(args.host, args.port, ' '.join(args.command), args.timeout)
    sys.stdout.write(reply.decode('utf-8', errors='replace'))
    if not reply.endswith(b'\n'):
        sys.stdout.write('\n')


if __name__ == '__main__':
    main()
