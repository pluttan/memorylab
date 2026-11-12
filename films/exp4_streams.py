"""Film 4: reading r arrays at once. Separate arrays are r address streams the prefetcher
must follow; one interleaved array is a single stream that uses every line in full."""
import sys
from pathlib import Path

from manim import RIGHT, Create, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (BLUE, DIM, GREEN, MAUVE, PEACH, RED, SURF, TEAL, YELLOW, Fade, Film, Plot, cell, code,  # noqa: E402
                    panel, txt)
from model import TRACKED, streams_curve  # noqa: E402

COLORS = [BLUE, PEACH, GREEN, MAUVE, YELLOW, TEAL]
SX0, SCW, SY0, SDY = -5.6, 0.3, 2.45, 0.38      # separate arrays: one strip per array
ELEMS = 8
PF_X, PF_Y = -1.35, 1.5
COUNTS = list(range(1, 33))


class Exp4Streams(Film):
    TITLE = 'Опыт 4. Эффективное чтение оперативной памяти'

    def story(self):
        self.sep, self.tog = streams_curve(COUNTS)
        self.say('Опыт читает r массивов одновременно: на каждом шаге по элементу из каждого.',
                 'Сравниваются два способа разместить те же данные в памяти.')
        self.draw_prefetcher()
        self.draw_plot()
        self.separate(TRACKED)
        self.separate_more(6)
        self.together(6)
        self.finish()

    # --- scene ----------------------------------------------------------

    def strip(self, k):
        y = SY0 - k * SDY
        cells = VGroup(*[cell(SX0 + (i + 0.5) * SCW, y, SCW - 0.03, 0.28) for i in range(ELEMS)])
        return VGroup(cells, txt(f'массив {k}', 13, COLORS[k]).move_to((SX0 - 0.1, y, 0), aligned_edge=RIGHT))

    def draw_prefetcher(self):
        self.pf = panel(PF_X, PF_Y, 2.1, 1.75, 'предвыборка')
        self.slots = []
        for s in range(TRACKED):
            c = cell(PF_X, PF_Y + 0.6 - s * 0.4, 1.8, 0.32, SURF)
            t = txt('свободно', 12, DIM).move_to(c)
            self.slots.append([c, t])
            self.pf.add(c, t)
        self.play(Create(self.pf), run_time=0.8)
        self.say('Аппаратная предвыборка замечает поток адресов и приносит линейки заранее.',
                 f'В модели она следит за {TRACKED} потоками сразу. У реальных процессоров потоков больше.')

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (1, 32), (0, 100), 'количество массивов r', 'время',
                         xticks=[(v, str(v)) for v in (1, 4, 8, 16, 24, 32)])
        self.play(Create(self.plot), run_time=0.8)

    def take_slot(self, s, name, color):
        c, t = self.slots[s]
        new = txt(name, 12, color).move_to(c)
        self.slots[s][1] = new
        return [Fade(t, False), Fade(new, True), c.animate.set_fill(color, 0.25)]

    def free_slots(self):
        anims = []
        for s, (c, t) in enumerate(self.slots):
            new = txt('свободно', 12, DIM).move_to(c)
            self.slots[s][1] = new
            anims += [Fade(t, False), Fade(new, True), c.animate.set_fill(SURF, 1)]
        return anims

    # --- separate arrays --------------------------------------------------

    def separate(self, r):
        self.prog = code(['for (a = 0; a < n; a++)', '    for (b = 0; b < r; b++)', '        x += px[b][a];'],
                         -6.55, -0.2)
        self.strips = VGroup(*[self.strip(k) for k in range(r)])
        self.play(Create(self.strips), Fade(self.prog, True), run_time=1)
        self.say('Неоптимизированная структура: r отдельных массивов в разных местах памяти.',
                 'Каждый массив даёт свой поток адресов.')
        self.play(*[a for k in range(r) for a in self.take_slot(k, f'массив {k}', COLORS[k])], run_time=0.6)
        for i in range(4):
            self.play(*[self.strips[k][0][i].animate.set_fill(COLORS[k], 0.85) for k in range(r)],
                      *[self.strips[k][0][i + 1].animate.set_fill(COLORS[k], 0.3) for k in range(r)], run_time=0.3)
        dot = self.plot.dot(r, self.sep[r - 1], RED)
        self.say(f'При r = {r} предвыборка успевает за всеми потоками.',
                 'Но каждый шаг читает 4 байта из r разных линеек.', extra=[Create(dot)])
        self.dots = VGroup(dot)

    def separate_more(self, r):
        more = VGroup(*[self.strip(k) for k in range(len(self.strips), r)])
        self.play(Create(more), run_time=0.6)
        self.strips.add(*more)
        for i in range(4, 8):
            reads = [self.strips[k][0][i].animate.set_fill(COLORS[k] if k < TRACKED else RED, 0.85)
                     for k in range(r)]
            self.play(*reads, run_time=0.3 if i > 4 else 0.6)
        dot = self.plot.dot(r, self.sep[r - 1], RED)
        self.say(f'При r = {r} потоков больше, чем ячеек предвыборки. Лишние массивы ждут память.',
                 'Каждый поток к тому же держит открытой свою строку памяти.', extra=[Create(dot)])
        self.dots.add(dot)
        self.play(Fade(self.strips, False), Fade(self.prog, False), *self.free_slots(), run_time=0.5)

    # --- interleaved array ------------------------------------------------

    def together(self, r):
        self.prog = code(['for (a = 0; a < n * r; a += r)', '    for (b = 0; b < r; b++)',
                          '        x += p[a + b];'], -6.55, -0.2)
        n = 4
        cw = 0.16
        cells = VGroup(*[cell(-6.6 + (i + 0.5) * cw, 1.9, cw - 0.03, 0.4) for i in range(r * n)])
        names = VGroup(*[txt(str(i % r), 11, DIM).move_to(cells[i].get_center() + (0, 0.33, 0))
                         for i in range(r * n)])
        self.play(Create(cells), Fade(names, True), Fade(self.prog, True), run_time=0.8)
        self.say('Оптимизированная структура кладёт элементы всех массивов рядом.',
                 'Над ячейкой номер массива. Шаг читает r соседних слов.')
        self.play(*self.take_slot(0, 'один поток', GREEN), run_time=0.4)
        for i in range(n):
            self.play(*[cells[i * r + k].animate.set_fill(COLORS[k], 0.85) for k in range(r)], run_time=0.35)
        dot = self.plot.dot(r, self.tog[r - 1], GREEN)
        self.say('Это один поток адресов. Каждая принесённая линейка используется целиком.',
                 'Предвыборке хватает одной ячейки при любом r.', extra=[Create(dot)])
        self.dots.add(dot)
        self.play(Fade(VGroup(cells, names), False), run_time=0.4)

    def finish(self):
        red = self.plot.curve(COUNTS, self.sep, RED)
        green = self.plot.curve(COUNTS, self.tog, GREEN)
        legend = self.plot.legend([('отдельные массивы', RED), ('один массив', GREEN)])
        self.say('Оба графика растут: с каждым массивом данных больше.',
                 'Красный растёт быстрее, когда потоков больше, чем умеет предвыборка.',
                 extra=[Create(red, run_time=2), Create(green, run_time=2), Fade(legend, True),
                        Fade(self.dots, False)], run_time=2)
        ratio = sum(self.sep) / sum(self.tog)
        self.say(f'Опыт делит суммарное время отдельных массивов на время одного. В модели вышло {ratio:.1f}.',
                 'Вывод: данные, которые читаются вместе, выгодно хранить рядом.')
        self.play(Fade(self.prog, False), run_time=0.3)
