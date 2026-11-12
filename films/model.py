"""Toy models of memory behind the films: they give the curves the films draw.

The numbers are not measurements. Each model keeps one effect of the real
experiment, so the curve has the shape the student sees on a real machine.
Time is in model units: a hit in an open DRAM row costs 1.
"""
import math
from dataclasses import dataclass

# ==========================
# ===  Experiment 1: DRAM ===
# ==========================


@dataclass(frozen=True)
class Dram:
    """DRAM of the film: lines interleave over banks, a bank keeps one row open."""
    banks: int = 4
    cols: int = 8          # lines in one row of one bank
    rows: int = 4
    line: int = 64
    hit: float = 1.0       # read from the open row
    miss: float = 3.0      # close the row, open another, read
    gap: float = 0.25      # the core issues one read per gap
    window: int = 4        # reads in flight at once (line fill buffers)

    @property
    def size(self):
        return self.banks * self.cols * self.rows * self.line

    def place(self, addr):
        """(bank, row, column) of the line that holds addr."""
        n = addr // self.line
        return n % self.banks, n // (self.banks * self.cols) % self.rows, n // self.banks % self.cols


def stratification_reads(dram, step, inner=None):
    """Addresses in the order of the experiment loop: for b in 0..step by line, a = b, b + step, ..."""
    out = []
    for b in range(0, step, dram.line):
        for a in range(b, dram.size, step):
            out.append(a)
    return out if inner is None else out[:inner]


def dram_time(dram, addrs):
    """Time of the reads: banks work in parallel, at most `window` reads are in flight,
    a bank serves its reads one by one and pays a miss when the row changes."""
    free = [0.0] * dram.banks
    opened = [None] * dram.banks
    done = []
    issue = 0.0
    for i, addr in enumerate(addrs):
        if i >= dram.window:
            issue = max(issue, done[i - dram.window])
        bank, row, _ = dram.place(addr)
        start = max(issue, free[bank])
        cost = dram.hit if opened[bank] == row else dram.miss
        opened[bank] = row
        free[bank] = start + cost
        done.append(free[bank])
        issue += dram.gap
    return max(done) if done else 0.0


def stratification_curve(dram, steps):
    return [dram_time(dram, stratification_reads(dram, s)) for s in steps]


# ==========================
# ===  Experiment 2: list ===
# ==========================
LINE, PAGE = 64, 4096
NODE = 16                       # next pointer and value, as in list_vs_array.cpp


def list_node_cost(dist):
    """Time of one list step when nodes are dist bytes apart.

    The next address comes only from the node just read, so every step waits for its read.
    A node in the line of the previous one is in L1. A node in a new line waits for memory;
    within a page the stride prefetcher fetches the next line early, past a page it stops,
    and the step also pays a TLB miss."""
    same_line = max(0.0, 1 - dist / LINE)
    new_line = 1 - same_line
    if dist < PAGE:
        return same_line * 0.5 + new_line * 3.0
    return 6.0 + 3.0


ARRAY_COST = 0.25               # independent reads: the core runs ahead, lines come by prefetch


def list_curve(dists, n=1):
    return [n * list_node_cost(d) for d in dists], [n * ARRAY_COST for _ in dists]


# ==========================
# ===  Experiment 3: TLB  ===
# ==========================
WALK = 8.0                      # page table walk: extra reads of memory on a TLB miss


def tlb_reads(block=4 * PAGE, step=256):
    return list(range(0, block, step))


def tlb_times(offsets, prefetched):
    """Time of every read: the first read of a page walks the page table unless the
    prefetch loop has already loaded the page into the TLB."""
    out = []
    for i, a in enumerate(offsets):
        first = i == 0 or a // PAGE != offsets[i - 1] // PAGE
        out.append(1.0 + (0.5 if prefetched else WALK) if first else 1.0)
    return out


# ==========================
# ===  Experiment 4: streams ===
# ==========================
TRACKED = 4                     # streams the prefetcher follows at once in the film


def streams_curve(counts):
    """Time to read r arrays element by element: one interleaved array is one stream;
    r separate arrays are r streams, and past TRACKED of them every line waits for memory."""
    separate, together = [], []
    for r in counts:
        together.append(float(r))
        extra = min(2.0, 0.12 * max(0, r - TRACKED))
        separate.append(r * (1.0 + extra))
    return separate, together


# ==========================
# ===  Experiment 5: conflicts ===
# ==========================


@dataclass(frozen=True)
class Cache:
    sets: int = 8
    ways: int = 4
    line: int = 64
    hit: float = 1.0
    miss: float = 10.0

    @property
    def bank(self):
        """One way of all sets: addresses this far apart fall into the same set."""
        return self.sets * self.line

    def set_of(self, addr):
        return addr // self.line % self.sets


def lru_run(cache, addrs, rounds=4):
    """Read addrs in a circle; returns per-read hit flags of the last round and the
    contents of every set after it (oldest first)."""
    sets = [[] for _ in range(cache.sets)]
    flags = []
    for r in range(rounds):
        flags = []
        for a in addrs:
            tag = a // cache.line
            s = sets[cache.set_of(a)]
            hit = tag in s
            if hit:
                s.remove(tag)
            elif len(s) == cache.ways:
                s.pop(0)
            s.append(tag)
            flags.append(hit)
    return flags, sets


def conflict_time(cache, stride, n):
    flags, _ = lru_run(cache, [k * stride for k in range(n)])
    return sum(cache.hit if h else cache.miss for h in flags) / n


# ==========================
# ===  Experiment 6: cache levels ===
# ==========================
# Time per KB on i5-8400 (pcomp), 08.11.2026: L1, L2, L3, DRAM
LEVELS = [32 << 10, 256 << 10, 9 << 20]
MODES = {
    'read': [3.1, 8.9, 16.3, 44.0],
    'write': [5.3, 14.8, 26.6, 69.0],
    'read_write': [6.6, 15.2, 24.9, 71.0],
    'write_read': [8.5, 24.6, 42.8, 112.0],
}


def cache_time(size, costs, levels=LEVELS):
    """Time per KB of a block of `size` bytes read over and over: the part that does not fit
    a level comes from the next one (the block is shared out by the replacement policy)."""
    t, rest, prev = 0.0, size, 0
    for cap, cost in zip(levels, costs):
        part = min(rest, cap - prev) if cap > prev else 0
        t += part * cost
        rest -= part
        prev = cap
    t += rest * costs[-1]
    return t / size


def cache_curve(sizes, mode='read'):
    return [cache_time(s, MODES[mode]) for s in sizes]


# ==========================
# ===  Experiment 7: sorting ===
# ==========================
def sort_curve(ns):
    """QuickSort pays n log n compares, half of them mispredicted on random data;
    Radix makes 8 passes of counting and moving; ERadix counts all 8 digits in one pass."""
    quick = [n * math.log2(n) * 1.0 for n in ns]
    radix = [n * 8 * (0.5 + 1.5) for n in ns]
    eradix = [n * (1.5 + 8 * 1.5) for n in ns]
    return quick, radix, eradix


# ==========================
# ===  Experiment 8: JIT  ===
# ==========================


def jit_frames(frames=210, per_mode=35, jit_ms=1.0, branch_ms=1.32, seed=7):
    """Frame times of the DOOM run: modes switch every second (per_mode frames), noise
    from a fixed LCG so the film is the same every render."""
    out, x = [], seed
    for f in range(frames):
        jit = (f // per_mode) % 2 == 0
        x = (1103515245 * x + 12345) % (1 << 31)
        noise = (x / (1 << 31) - 0.5) * 0.12
        out.append((jit, (jit_ms if jit else branch_ms) * (1 + noise)))
    return out
