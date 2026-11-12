"""Film 7: QuickSort against Radix-Counting sort. QuickSort compares and swaps, n log n times;
Radix counts digits and moves every number straight to its place, a fixed number of passes."""
import sys
from pathlib import Path

import numpy as np
from manim import PI, RIGHT, AnimationGroup, Create, FadeOut, LaggedStart, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (BLUE, DIM, GREEN, MAUVE, PEACH, RED, SUB, TEXT, YELLOW, Film, Plot, blink, cell, hide,  # noqa: E402
                    roll, show, txt)
from model import sort_curve  # noqa: E402

DATA = [53, 17, 82, 37, 61, 28, 74, 13]
AX, AW, AY = -6.3, 0.75, 2.35       # array cells
TX, TW, TY = -5.5, 0.6, 1.05        # count table
OY = -0.2                           # output array
NS = list(np.linspace(0.5e6, 10e6, 40))


def num(v, color=TEXT, hl=None, hl_color=YELLOW):
    """Two digits as separate letters, so one of them can be lit."""
    d = [txt(ch, 18, hl_color if hl == i else color) for i, ch in enumerate(f'{v:02d}')]
    return VGroup(*d).arrange((1, 0, 0), buff=0.02)


class Exp7Sort(Film):
    TITLE = 'Опыт 7. Сравнение алгоритмов сортировки'

    def story(self):
        self.draw_array(DATA)
        self.quicksort()
        self.radix()
        self.eradix()
        self.graph()

    # --- array ------------------------------------------------------------

    def apos(self, i, y=AY):
        return np.array((AX + i * AW, y, 0.0))

    def draw_array(self, values):
        self.boxes = VGroup(*[cell(*self.apos(i)[:2], AW - 0.08, 0.5) for i in range(len(values))])
        self.nums = [num(v).move_to(self.apos(i)) for i, v in enumerate(values)]
        self.values = list(values)
        appear = LaggedStart(*[show(VGroup(b, n), lag=0.3) for b, n in zip(self.boxes, self.nums)], lag_ratio=0.12)
        self.say('Опыт сортирует массивы 64-битных чисел тремя способами и сравнивает время.',
                 'В ролике числа двузначные, а разряд равен одной цифре.', extra=[appear], run_time=1.6)

    def swap(self, i, j):
        if i == j:
            return
        a, b = self.nums[i], self.nums[j]
        # The same arc angle sends one number over the row and the other under it
        self.play(a.animate(path_arc=PI / 2).move_to(self.apos(j)),
                  b.animate(path_arc=PI / 2).move_to(self.apos(i)), run_time=0.8)
        self.nums[i], self.nums[j] = b, a
        self.values[i], self.values[j] = self.values[j], self.values[i]

    def flash(self, i, color, rt=0.4):
        self.play(blink(self.boxes[i], color, 0.5), run_time=rt)

    # --- QuickSort --------------------------------------------------------

    def quicksort(self):
        lo, hi = 0, len(self.values) - 1
        mid = self.values[(lo + hi) >> 1]
        pivot = self.frame(self.boxes[(lo + hi) >> 1], MAUVE)
        self.say(f'QuickSort берёт опорное число {mid} и сравнивает с ним остальные.',
                 'Меньшие числа уходят влево, большие вправо.', extra=[Create(pivot)], hold=1.5)
        while True:
            while self.values[lo] < mid:
                self.flash(lo, GREEN)
                lo += 1
            self.flash(lo, RED)
            while self.values[hi] > mid:
                self.flash(hi, GREEN)
                hi -= 1
            self.flash(hi, RED)
            if lo <= hi:
                self.swap(lo, hi)
                lo, hi = lo + 1, hi - 1
            if lo >= hi:
                break
        self.play(FadeOut(pivot), run_time=0.4)
        self.say('Каждое сравнение даёт условный переход. Сравнений n·log n.',
                 'На случайных числах процессор часто не угадывает переход и сбрасывает конвейер.')
        order = sorted(range(len(self.values)), key=lambda i: self.values[i])
        self.say('Дальше QuickSort так же делит левую и правую части, пока массив не упорядочен.', hold=0)
        self.play(LaggedStart(*[self.nums[i].animate(path_arc=PI / 3).move_to(self.apos(k))
                                for k, i in enumerate(order)], lag_ratio=0.1), run_time=1.6)
        self.wait(1.5)
        self.play(hide(*self.nums), run_time=0.5)

    # --- Radix ------------------------------------------------------------

    def table(self, y, title):
        boxes = VGroup(*[cell(TX + d * TW, y, TW - 0.08, 0.42) for d in range(10)])
        heads = VGroup(*[txt(str(d), 13, DIM).move_to((TX + d * TW, y + 0.38, 0)) for d in range(10)])
        name = txt(title, 14, SUB).move_to((TX - 0.3, y + 0.38, 0), aligned_edge=RIGHT)
        counts = [txt('0', 15, TEXT).move_to(b) for b in boxes]
        return VGroup(boxes, heads, name), counts

    def set_count(self, counts, boxes, d, value, color=TEXT):
        new = txt(str(value), 15, color).move_to(boxes[d])
        anims = roll(counts[d], new)
        counts[d] = new
        return anims

    def radix_pass(self, digit, fast):
        rt = 0.4 if fast else 0.7
        self.play(*[n[1 - digit].animate.set_color(TEXT) for n in self.nums],
                  *[n[digit].animate.set_color(YELLOW) for n in self.nums], run_time=0.5)
        if not fast:
            self.say('Первый проход считает, сколько чисел с каждой цифрой.', hold=0)
        c = [0] * 10
        for i, v in enumerate(self.values):
            d = int(f'{v:02d}'[digit])
            c[d] += 1
            self.play(*self.count_one(self.nums[i][digit], self.tab[0], self.counts, d, c[d]),
                      blink(self.boxes[i], YELLOW, 0.35), run_time=rt)
        for d in range(1, 10):
            c[d] += c[d - 1]
        if not fast:
            self.say('Суммы с накоплением дают место, где кончается группа каждой цифры.', hold=0)
        self.play(LaggedStart(*[AnimationGroup(*self.set_count(self.counts, self.tab[0], d, c[d], PEACH))
                                for d in range(10)], lag_ratio=0.12), run_time=1.2)
        if not fast:
            self.wait(2)
            self.say('Второй проход ставит каждое число прямо на его место.',
                     'Чисел Radix не сравнивает, и условных переходов почти нет.', hold=0)
        out, pos = [None] * len(self.values), [0] * len(self.values)
        outbox = VGroup(*[cell(*self.apos(i, OY)[:2], AW - 0.08, 0.5) for i in range(len(self.values))])
        self.play(show(outbox, lag=0.05), run_time=0.5)
        for i in range(len(self.values) - 1, -1, -1):
            v = self.values[i]
            d = int(f'{v:02d}'[digit])
            c[d] -= 1
            out[c[d]], pos[i] = v, c[d]
            self.play(self.nums[i].animate(path_arc=-PI / 4).move_to(self.apos(c[d], OY)),
                      *self.set_count(self.counts, self.tab[0], d, c[d], PEACH), run_time=rt)
        if not fast:
            self.wait(1.5)
        # The output row becomes the input of the next pass
        new_nums = [None] * len(self.values)
        for i, n in enumerate(self.nums):
            new_nums[pos[i]] = n
        self.nums, self.values = new_nums, out
        self.play(*[n.animate.move_to(self.apos(k)) for k, n in enumerate(self.nums)], hide(outbox),
                  *[a for d in range(10) for a in self.set_count(self.counts, self.tab[0], d, 0)], run_time=0.8)

    def count_one(self, digit_mob, boxes, counts, d, value):
        """A copy of the digit flies into column d of the table, the count rolls up."""
        flyer = digit_mob.copy()
        return [LaggedStart(flyer.animate(path_arc=-0.6).move_to(boxes[d]).set_opacity(0),
                            AnimationGroup(*self.set_count(counts, boxes, d, value)), lag_ratio=0.6),
                blink(boxes[d], YELLOW, 0.35)]

    def radix(self):
        self.values = list(DATA)
        self.nums = [num(v).move_to(self.apos(i)) for i, v in enumerate(self.values)]
        self.tab, self.counts = self.table(TY, 'счёт')
        self.play(LaggedStart(show(*self.nums, lag=0.06), show(self.tab, lag=0.2), show(*self.counts, lag=0.04),
                              lag_ratio=0.3), run_time=1.6)
        self.say('Radix-Counting Sort сортирует по разрядам, начиная с младшего.',
                 'Сначала по последней цифре, потом по первой.')
        self.radix_pass(1, fast=False)
        self.say('Массив упорядочен по последней цифре. Тот же проход по первой цифре.')
        self.radix_pass(0, fast=True)
        self.say('Массив упорядочен. Число проходов равно числу разрядов и не зависит от n.',
                 'В опыте разряд равен байту, у 64-битного числа 8 разрядов.')

    def eradix(self):
        tab2, counts2 = self.table(TY - 1.2, 'десятки')
        name = txt('единицы', 14, SUB).move_to(self.tab[2], aligned_edge=RIGHT)
        self.play(*roll(self.tab[2], name), LaggedStart(show(tab2, lag=0.2), show(*counts2, lag=0.04),
                                                        lag_ratio=0.3), run_time=1.2)
        self.say('Оптимизированный Radix считает таблицы всех разрядов за один проход.',
                 'Вместо 8 проходов счёта остаётся один.', hold=0)
        c1, c2 = [0] * 10, [0] * 10
        for i, v in enumerate(self.values):
            u, t = v % 10, v // 10
            c1[u] += 1
            c2[t] += 1
            self.play(*self.count_one(self.nums[i][1], self.tab[0], self.counts, u, c1[u]),
                      *self.count_one(self.nums[i][0], tab2[0], counts2, t, c2[t]),
                      blink(self.boxes[i], YELLOW, 0.35), run_time=0.45)
        self.wait(1.5)
        self.play(hide(tab2, *counts2, self.tab, name, *self.counts, *self.nums, self.boxes), run_time=0.7)

    # --- graph ------------------------------------------------------------

    def graph(self):
        q, r, e = sort_curve(NS)
        top = max(q) * 1.08
        plot = Plot(-6.4, -2.45, 13.0, 5.2, (0, 10e6), (0, top), 'элементов в массиве', 'время',
                    xticks=[(v * 1e6, f'{v} М') for v in (2, 4, 6, 8, 10)])
        lines = [plot.curve(NS, y, c) for y, c in ((q, MAUVE), (r, BLUE), (e, GREEN))]
        legend = plot.legend([('QuickSort', MAUVE), ('Radix-Counting', BLUE), ('Radix-Counting оптим.', GREEN)])
        self.play(plot.appear(), run_time=1.2)
        self.say('QuickSort растёт как n·log n, Radix растёт линейно.',
                 'На больших массивах Radix быстрее, и разница растёт с n.',
                 extra=[*[Create(ln, run_time=2.5) for ln in lines], show(legend, lag=0.15)], run_time=2.5)
        ratio = sum(q) / sum(r)
        self.say(f'Опыт делит время QuickSort на время Radix. В модели вышло {ratio:.2f}.',
                 'Вывод: алгоритм без сравнений выигрывает на больших массивах.')
