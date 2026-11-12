"""Film 2: a list against an array. Independent reads of an array overlap and come by
prefetch; every step of a list waits for its own read, and fragmentation makes it miss."""
import sys
from pathlib import Path

import numpy as np
from manim import PI, Create, CurvedArrow, Dot, MoveAlongPath, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (BLUE, DIM, GREEN, HL, PEACH, RED, SUB, SURF, YELLOW, Fade, Film, Plot, cell, code, txt)  # noqa: E402
from model import LINE, NODE, PAGE, list_curve  # noqa: E402

CW, CH = 0.37, 0.42             # one 16-byte element
X0, ROW_Y = -6.55, [1.75, 0.85]
PER_ROW = 16
DISTS = [16 * 2 ** k for k in range(16)]          # 16 B .. 512 KB


class Exp2List(Film):
    TITLE = 'Опыт 2. Ссылочные и векторные структуры'

    def story(self):
        self.lst, self.arr = list_curve(DISTS)
        self.say('Опыт ищет максимум в массиве и в списке из того же числа элементов.',
                 'Список читается дольше. Опыт показывает, во сколько раз и почему.')
        self.draw_memory()
        self.draw_plot()
        self.array_demo()
        self.list_demo(1, 'Узлы списка лежат подряд, через 16 байт. Каждый узел хранит адрес следующего.',
                       'Пока узел не прочитан, следующее чтение не начать. Чтения идут по одному.')
        self.list_demo(LINE // NODE, 'Узлы лежат через 64 байта: каждый узел в новой линейке.',
                       'Каждый шаг ждёт оперативную память, время растёт.')
        self.pages_demo()
        self.finish()

    # --- memory ---------------------------------------------------------

    def pos(self, k):
        return np.array((X0 + (k % PER_ROW + 0.5) * CW, ROW_Y[k // PER_ROW], 0.0))

    def draw_memory(self):
        self.cells = VGroup(*[cell(*self.pos(k)[:2], CW - 0.04, CH) for k in range(2 * PER_ROW)])
        # A frame around every four elements: one cache line
        self.lines = VGroup()
        for k in range(0, 2 * PER_ROW, 4):
            a, b = self.pos(k), self.pos(k + 3)
            self.lines.add(cell((a[0] + b[0]) / 2, a[1], 4 * CW, CH + 0.14, SURF, opacity=0, stroke=DIM))
        ticks = VGroup(*[txt(f'{k * NODE}', 11, DIM).move_to(self.pos(k) + (0, CH / 2 + 0.2, 0))
                         for k in range(0, 2 * PER_ROW, 4)])
        self.play(Create(self.lines), Create(self.cells), Fade(ticks, True), run_time=1.2)
        self.ticks = ticks
        self.spot([self.lines[0]], 'Память отдаёт данные линейками по 64 байта.',
                  'В одну линейку входят четыре элемента по 16 байт. Сверху смещение в байтах.')

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (16, 524288), (0, 10), 'фрагментация, байт', 'время',
                         xticks=[(16, '16'), (64, '64'), (1024, '1 К'), (4096, '4 К'), (65536, '64 К'),
                                 (524288, '512 К')], xlog=True)
        self.play(Create(self.plot), run_time=1)

    def reset(self, extra=()):
        self.play(*[c.animate.set_fill(SURF, 1) for c in self.cells], *extra, run_time=0.4)

    # --- array ----------------------------------------------------------

    def array_demo(self):
        prog = code(['for (i = 0; i < n; i++)', '    if (max < arr[i]) max = arr[i];'], -6.55, -0.25)
        self.play(Fade(prog, True), run_time=0.5)
        self.say('Массив лежит подряд. Адрес следующего элемента известен заранее.',
                 'Процессор читает несколько элементов сразу, не дожидаясь первого.', hold=1.5)
        for line in range(4):
            reads = [self.cells[4 * line + i].animate.set_fill(GREEN, 0.85) for i in range(4)]
            ahead = [self.lines[line + 1].animate.set_stroke(BLUE, 3)] if line < 3 else []
            self.play(*reads, *ahead, run_time=0.35)
        green = self.plot.curve(DISTS, self.arr, GREEN)
        self.say('Предвыборка заранее приносит следующую линейку, она горит синим.',
                 'Время массива не зависит от фрагментации: это зелёная линия.', extra=[Create(green)])
        self.reset([ln.animate.set_stroke(DIM, 1.5) for ln in self.lines] + [Fade(prog, False)])
        self.green = green

    # --- list -----------------------------------------------------------

    def list_demo(self, gap, line, second):
        """Walk the list with nodes `gap` elements apart; every hop waits for its read."""
        if gap == 1:
            self.prog = code(['while ((node = node->next))',
                              '    if (max < node->val) max = node->val;'], -6.55, -0.25)
            self.play(Fade(self.prog, True), run_time=0.5)
        nodes = list(range(0, 2 * PER_ROW, gap))[:8]
        token = Dot(self.pos(nodes[0]), radius=0.09, color=YELLOW).set_z_index(5)
        arrows = VGroup()
        self.play(Create(token), self.cells[nodes[0]].animate.set_fill(PEACH, 0.85), run_time=0.3)
        for a, b in zip(nodes, nodes[1:]):
            arc = CurvedArrow(self.pos(a) + (0, CH / 2, 0), self.pos(b) + (0, CH / 2, 0), angle=-PI / 2.2,
                              color=SUB, stroke_width=2, tip_length=0.12)
            if self.pos(a)[1] != self.pos(b)[1]:
                arc = CurvedArrow(self.pos(a) + (0.1, -CH / 2, 0), self.pos(b) + (0, CH / 2, 0), angle=0,
                                  color=SUB, stroke_width=2, tip_length=0.12)
            arrows.add(arc)
            miss = self.pos(a)[0] // 1 != self.pos(b)[0] // 1 and (b // 4 != a // 4)
            color = RED if miss else PEACH
            self.play(Create(arc), MoveAlongPath(token, arc), run_time=0.5 if miss else 0.3)
            self.play(self.cells[b].animate.set_fill(color, 0.85), run_time=0.45 if miss else 0.15)
        dist = gap * NODE
        dot = self.plot.dot(dist, self.lst[DISTS.index(dist)], RED)
        self.say(line, second, extra=[Create(dot)])
        self.dots = getattr(self, 'dots', VGroup()).add(dot)
        self.reset([Fade(arrows, False), Fade(token, False)])

    def pages_demo(self):
        """Nodes on different pages: four page boxes, one node in each."""
        self.play(Fade(self.cells, False), Fade(self.lines, False), Fade(self.ticks, False), run_time=0.4)
        pages = VGroup()
        for p in range(4):
            x = X0 + 0.75 + p * 1.55
            box = cell(x, 1.3, 1.35, 1.3, SURF, stroke=SUB)
            pages.add(VGroup(box, txt(f'страница {p}', 13, SUB).move_to((x, 2.15, 0)),
                             txt('4 КБ', 12, DIM).move_to((x, 0.5, 0))))
        self.play(Create(pages), run_time=0.8)
        nodes = [cell(X0 + 0.75 + p * 1.55 + (p % 2 - 0.5) * 0.4, 1.3 + (0.5 - p % 2) * 0.3, CW - 0.04, CH,
                      SURF) for p in range(4)]
        self.play(*[Create(n) for n in nodes], run_time=0.4)
        token = Dot(nodes[0].get_center(), radius=0.09, color=YELLOW).set_z_index(5)
        self.play(Create(token), nodes[0].animate.set_fill(RED, 0.85), run_time=0.3)
        arrows = VGroup()
        for a, b in zip(nodes, nodes[1:]):
            arc = CurvedArrow(a.get_top(), b.get_top(), angle=-PI / 2.5, color=SUB, stroke_width=2, tip_length=0.12)
            arrows.add(arc)
            self.play(Create(arc), MoveAlongPath(token, arc), run_time=0.6)
            self.play(b.animate.set_fill(RED, 0.85), run_time=0.6)
        dot = self.plot.dot(8192, self.lst[DISTS.index(8192)], RED)
        self.say('Узлы на разных страницах: каждый шаг ищет страницу в буфере TLB.',
                 'Предвыборка не переходит границу страницы и больше не помогает.', extra=[Create(dot)])
        self.dots.add(dot)
        self.play(Fade(VGroup(pages, *nodes, arrows, token), False), run_time=0.4)

    def finish(self):
        red = self.plot.curve(DISTS, self.lst, RED)
        legend = self.plot.legend([('список', RED), ('массив', GREEN)], y=self.plot.y0 + self.plot.h * 0.55)
        self.say('Красная линия: список. Ступени стоят на линейке 64 байта и странице 4 КБ.',
                 'Список при любой фрагментации медленнее массива.',
                 extra=[Create(red, run_time=2.5), Fade(self.dots, False), Fade(legend, True)], run_time=2.5)
        marks = VGroup(self.plot.mark(LINE, 'линейка', HL, 13), self.plot.mark(PAGE, 'страница', HL, 13))
        self.play(Create(marks), Fade(self.prog, False), run_time=0.6)
        ratio = sum(self.lst) / sum(self.arr)
        self.say(f'Опыт делит суммарное время списка на время массива. В модели вышло {ratio:.0f}.',
                 'Вывод: чтения, которые не зависят друг от друга, процессор выполняет одновременно.')
