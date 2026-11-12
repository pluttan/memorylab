"""Film 5: conflicts in a set-associative cache. A stride equal to the cache bank sends every
line into one set; one more line of stride spreads them over all sets."""
import sys
from pathlib import Path

import numpy as np
from manim import (LEFT, RIGHT, UP, AnimationGroup, Create, FadeIn, FadeOut, Indicate, LaggedStart, VGroup,
                   there_and_back)

sys.path.insert(0, str(Path(__file__).parent))
from common import (DIM, GREEN, HL, RED, SURF, TEXT, YELLOW, Film, Plot, blink, cell, code, draw, pop, show,  # noqa: E402
                    txt)
from model import Cache, conflict_time  # noqa: E402

CACHE = Cache()
CX0, CW, CH = -5.55, 0.66, 0.3
CY0, CDY = 2.3, 0.34
NS = list(range(1, 17))


class Exp5Conflicts(Film):
    TITLE = 'Опыт 5. Конфликты в кэш-памяти'

    def story(self):
        self.red = [conflict_time(CACHE, CACHE.bank, n) for n in NS]
        self.green = [conflict_time(CACHE, CACHE.bank + CACHE.line, n) for n in NS]
        self.draw_cache()
        self.draw_plot()
        self.dots = VGroup()
        self.run(CACHE.bank, 6, RED, [
            (1, 'Шаг 512 байт равен банку. Все линейки попадают в набор 0.',
             'Четыре линейки заняли четыре пути набора, остальные наборы пусты.'),
            (5, 'Пятая линейка вытесняет самую старую, шестая следующую.',
             'При повторном проходе каждое чтение промахивается.')])
        self.run(CACHE.bank + CACHE.line, 6, GREEN, [
            (1, 'Шаг 576 байт, на линейку больше банка. Линейки идут в разные наборы.',
             'Все шесть помещаются, повторный проход читает только из кэша.')])
        self.finish()

    # --- cache table ------------------------------------------------------

    def pos(self, s, w):
        return (CX0 + (w + 0.5) * CW, CY0 - s * CDY, 0)

    def draw_cache(self):
        self.cells = [[cell(*self.pos(s, w)[:2], CW - 0.05, CH) for w in range(CACHE.ways)]
                      for s in range(CACHE.sets)]
        self.labels = [[None] * CACHE.ways for _ in range(CACHE.sets)]
        table = VGroup(*[c for row in self.cells for c in row])
        sets = VGroup(*[txt(f'набор {s}', 12, DIM).move_to((CX0 - 0.08, CY0 - s * CDY, 0), aligned_edge=RIGHT)
                        for s in range(CACHE.sets)])
        ways = VGroup(*[txt(f'путь {w}', 12, DIM).move_to((CX0 + (w + 0.5) * CW, CY0 + 0.3, 0))
                        for w in range(CACHE.ways)])
        rule = txt('набор = (адрес / 64) mod 8', 15, TEXT).move_to((CX0 - 0.95, -0.55, 0), aligned_edge=LEFT)
        rows = [VGroup(sets[r], *self.cells[r]) for r in range(CACHE.sets)]
        appear = LaggedStart(show(ways, lag=0.1), *[show(r, lag=0.08) for r in rows], lag_ratio=0.12)
        self.say('Опыт показывает, как шаг чтения меняет работу кэша.',
                 'При неудачном шаге большая часть кэша простаивает.', extra=[appear], run_time=2)
        self.say('Кэш делится на наборы. Линейка ложится только в свой набор, в любой из 4 путей.',
                 'Номер набора задают биты адреса сразу за смещением в линейке.', extra=[show(rule)])
        column = VGroup(*[row[0] for row in self.cells])
        self.spot([column], 'Банк кэша: один путь всех наборов. Здесь 8 линеек по 64 байта, 512 байт.',
                  'Адреса, которые отстоят на банк, попадают в один набор.')
        self.prog = code(self.listing('step'), CX0 - 0.95, -1.15)
        self.play(show(self.prog, lag=0.2), run_time=0.7)

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (1, 16), (0, 11), 'читаемых линеек n',
                         'среднее время чтения', xticks=[(v, str(v)) for v in (1, 4, 5, 8, 12, 16)])
        self.play(self.plot.appear(), run_time=1.2)

    # --- one stride -------------------------------------------------------

    def put(self, line, s, w, color):
        """Line `line` flies in from the right of its set into way w; a line it evicts flies out."""
        anims = []
        cell_ = self.cells[s][w]
        old = self.labels[s][w]
        if old is not None:
            anims.append(FadeOut(old, shift=UP * 0.45, scale=0.6))
            anims.append(cell_.animate(rate_func=there_and_back).set_fill(RED, 0.9))
        lab = txt(f'L{line}', 13, TEXT).move_to(np.array(self.pos(s, CACHE.ways)) + RIGHT * 0.5)
        self.labels[s][w] = lab
        anims.append(FadeIn(lab, shift=LEFT * 0.2))
        return anims, lab, cell_

    def land(self, lab, cell_, color):
        return AnimationGroup(lab.animate(path_arc=0.3).move_to(cell_), cell_.animate.set_fill(color, 0.35),
                              lag_ratio=0.5)

    @staticmethod
    def listing(step):
        lit = YELLOW if isinstance(step, int) else TEXT
        return ['for (r = 0; r < rounds; r++)', '    for (k = 0; k < n; k++)', (f'        x += p[k * {step}];', lit)]

    def run(self, stride, n, color, notes):
        """Read n lines at this stride twice, LRU in every set."""
        self.prog, recode = self.recode(self.prog, self.listing(stride), CX0 - 0.95, -1.15)
        sets = [[] for _ in range(CACHE.sets)]          # (line, way), oldest first
        notes = dict((k, (a, b)) for k, a, b in notes)
        for rnd in range(2):
            for k in range(n):
                if rnd == 0 and k + 1 in notes:
                    self.say(*notes[k + 1], hold=0, extra=recode)
                    recode = []
                s = CACHE.set_of(k * stride)
                hit = any(line == k for line, _ in sets[s])
                if hit:
                    way = next(w for line, w in sets[s] if line == k)
                    sets[s] = [(line, w) for line, w in sets[s] if line != k] + [(k, way)]
                    c = self.cells[s][way]
                    self.play(blink(c, GREEN, 0.9), Indicate(self.labels[s][way], color=GREEN, scale_factor=1.3),
                              run_time=0.45)
                    continue
                if len(sets[s]) == CACHE.ways:
                    _, way = sets[s].pop(0)
                else:
                    way = len(sets[s])
                sets[s].append((k, way))
                anims, lab, cell_ = self.put(k, s, way, color)
                extra = []
                if rnd == 0:
                    self.dots.add(self.plot.dot(k + 1, conflict_time(CACHE, stride, k + 1), color))
                    extra = [pop(self.dots[-1])]
                self.play(LaggedStart(AnimationGroup(*anims), self.land(lab, cell_, color), lag_ratio=0.45),
                          *extra, run_time=0.9 if rnd == 0 else 0.7)
        self.wait(2)
        self.clear()

    def clear(self):
        anims = []
        for s in range(CACHE.sets):
            for w in range(CACHE.ways):
                anims.append(self.cells[s][w].animate.set_fill(SURF, 1))
                if self.labels[s][w] is not None:
                    anims.append(FadeOut(self.labels[s][w], shift=RIGHT * 0.3))
                    self.labels[s][w] = None
        self.play(*anims, run_time=0.4)

    def finish(self):
        red = self.plot.curve(NS, self.red, RED)
        green = self.plot.curve(NS, self.green, GREEN)
        legend = self.plot.legend([('шаг = банк', RED), ('шаг = банк + линейка', GREEN)],
                                  x=self.plot.x0 + 2.6, y=self.plot.y0 + self.plot.h * 0.5)
        self.say('Красный график поднимается на n = 5: линеек стало больше, чем путей в наборе.',
                 'Значит, степень ассоциативности равна 5 − 1 = 4.',
                 extra=[Create(red, run_time=2), Create(green, run_time=2), show(legend, lag=0.2),
                        FadeOut(self.dots, scale=0.3)], run_time=2)
        self.play(draw(self.plot.mark(5, 'n = 5', HL, 13)), run_time=0.6)
        ratio = self.red[-1] / self.green[-1]
        self.say(f'Опыт делит время с конфликтами на время без них. В модели вышло {ratio:.0f}.',
                 'Вывод: шаг, кратный банку кэша, выгодно сдвинуть на одну линейку.')
