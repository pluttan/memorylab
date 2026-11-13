"""Re-encode an experiment film for labos: H.264 Baseline in an Annex B stream behind a small header.

Usage: python3.12 tools/mkfilm.py IN.mp4 OUT.lfilm HEIGHT

The labos decoder (h264bsd) knows the Baseline profile only. Every IDR picture repeats SPS and PPS,
and IDR pictures come every GOP frames, so the player can start from any of them.
Header, little endian: "LFILM\\0\\1\\0", u32 width, height, fps, frames, gop, then the stream.
"""
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

FPS = 30
GOP = 60


def probe_frames(path):
    out = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-count_frames',
                          '-show_entries', 'stream=width,height,nb_read_frames', '-of', 'json', str(path)],
                         check=True, capture_output=True, text=True).stdout
    s = json.loads(out)['streams'][0]
    return int(s['width']), int(s['height']), int(s['nb_read_frames'])


def main():
    src, dst, height = Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3])
    with tempfile.TemporaryDirectory() as tmp:
        raw = Path(tmp) / 'film.h264'
        subprocess.run([
            'ffmpeg', '-v', 'error', '-y', '-i', str(src),
            '-vf', f'scale=-2:{height}:flags=lanczos', '-r', str(FPS),
            '-c:v', 'libx264', '-profile:v', 'baseline', '-crf', '24', '-preset', 'slow', '-tune', 'animation',
            '-pix_fmt', 'yuv420p', '-g', str(GOP), '-keyint_min', str(GOP), '-sc_threshold', '0', '-bf', '0',
            '-x264-params', 'repeat-headers=1', '-an', '-f', 'h264', str(raw),
        ], check=True)
        width, h, frames = probe_frames(raw)
        dst.parent.mkdir(parents=True, exist_ok=True)
        with open(dst, 'wb') as f:
            f.write(b'LFILM\0\1\0' + struct.pack('<5I', width, h, FPS, frames, GOP))
            f.write(raw.read_bytes())
    print(f'{dst}: {width}x{h}, {frames} frames, {dst.stat().st_size // 1024} KB')


if __name__ == '__main__':
    main()
