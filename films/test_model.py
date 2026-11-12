"""Shape of every film curve: the effect each film explains must show up in its model."""
import unittest

from model import (LEVELS, PAGE, Cache, Dram, cache_curve, conflict_time, jit_frames, list_curve, sort_curve,
                   stratification_curve, streams_curve, tlb_reads, tlb_times)


class Shapes(unittest.TestCase):
    def test_dram_peaks(self):
        d = Dram(rows=64)
        steps = list(range(64, 4097, 64))
        t = dict(zip(steps, stratification_curve(d, steps)))
        t1, t2 = d.banks * d.line, d.banks * d.cols * d.line
        self.assertGreater(t[t1], 1.5 * max(t[64], t[192], t[320]))     # one bank at T1
        peaks = [t[s] for s in range(t1, t2 + 1, t1)]
        self.assertEqual(peaks, sorted(peaks))                           # peaks grow up to T2
        self.assertEqual(max(t.values()), t[t2])
        for s in range(t2, 4097, t1):
            self.assertGreater(t[s], 0.9 * t[t2])                       # and stay after it

    def test_list_steps(self):
        dists = [16 * 2 ** k for k in range(16)]
        lst, arr = list_curve(dists)
        self.assertEqual(lst, sorted(lst))
        self.assertEqual(len(set(arr)), 1)
        self.assertLess(lst[0], lst[dists.index(64)])
        self.assertLess(lst[dists.index(2048)], lst[dists.index(PAGE)])

    def test_tlb_spikes(self):
        offs = tlb_reads()
        cold, warm = tlb_times(offs, False), tlb_times(offs, True)
        firsts = [i for i, a in enumerate(offs) if a % PAGE == 0]
        self.assertTrue(all(cold[i] > 5 for i in firsts))
        self.assertLess(max(warm), 2)
        self.assertGreater(sum(cold) / sum(warm), 1.2)

    def test_streams(self):
        sep, tog = streams_curve(range(1, 33))
        self.assertTrue(all(a >= b for a, b in zip(sep, tog)))
        self.assertGreater(sep[-1] / tog[-1], 2)

    def test_conflicts(self):
        c = Cache()
        red = [conflict_time(c, c.bank, n) for n in range(1, 17)]
        green = [conflict_time(c, c.bank + c.line, n) for n in range(1, 17)]
        self.assertEqual(red[:c.ways], [c.hit] * c.ways)
        self.assertEqual(red[c.ways], c.miss)                            # one line more than the ways
        self.assertEqual(green, [c.hit] * 16)

    def test_cache_steps(self):
        for mode in ('read', 'write', 'read_write', 'write_read'):
            low, mid, high = cache_curve([LEVELS[0] // 2, LEVELS[1] // 2 + LEVELS[0], 4 * LEVELS[2]], mode)
            self.assertLess(low, mid)
            self.assertLess(mid, high)

    def test_sort(self):
        q, r, e = sort_curve([1 << 23])
        self.assertGreater(q[0], r[0])
        self.assertGreater(r[0], e[0])

    def test_jit(self):
        f = jit_frames()
        jit = [t for j, t in f if j]
        br = [t for j, t in f if not j]
        self.assertGreater(sum(br) / len(br) / (sum(jit) / len(jit)), 1.2)


if __name__ == '__main__':
    unittest.main()
