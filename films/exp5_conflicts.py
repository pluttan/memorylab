"""Film 5: conflicts in a set-associative cache. A stride equal to the cache bank sends every
line into one set; one more line of stride spreads them over all sets."""
import sys
from pathlib import Path

from manim import LEFT, RIGHT, Create, Succession, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import DIM, GREEN, HL, RED, SURF, TEXT, Fade, Film, Plot, cell, code, txt  # noqa: E402
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
        self.say('Опыт показывает, как шаг чтения меняет работу кэша.',
                 'При неудачном шаге большая часть кэша простаивает.')
        self.draw_cache()
        self.draw_plot()
        self.dots = VGroup()
        self.run(CACHE.bank, 6, RED, [
            (4, 'Шаг 512 байт равен банку. Все линейки попадают в набор 0.',
             'Четыре линейки заняли четыре пути набора, остальные наборы пусты.'),
            (6, 'Пятая линейка вытесняет самую старую, шестая следующую.',
             'При повторном проходе каждое чтение промахивается.')])
        self.run(CACHE.bank + CACHE.line, 6, GREEN, [
            (6, 'Шаг 576 байт, на линейку больше банка. Линейки идут в разные наборы.',
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
        self.play(Create(table), Fade(sets, True), Fade(ways, True), run_time=1.2)
        self.say('Кэш делится на наборы. Линейка ложится только в свой набор, в любой из 4 путей.',
                 'Номер набора задают биты адреса сразу за смещением в линейке.', extra=[Fade(rule, True)])
        column = VGroup(*[row[0] for row in self.cells])
        self.spot([column], 'Банк кэша: один путь всех наборов. Здесь 8 линеек по 64 байта, 512 байт.',
                  'Адреса, которые отстоят на банк, попадают в один набор.')
        self.prog = code(['for (r = 0; r < rounds; r++)', '    for (k = 0; k < n; k++)',
                          '        x += p[k * step];'], CX0 - 0.95, -1.15)
        self.play(Fade(self.prog, True), run_time=0.4)

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (1, 16), (0, 11), 'читаемых линеек n',
                         'среднее время чтения', xticks=[(v, str(v)) for v in (1, 4, 5, 8, 12, 16)])
        self.play(Create(self.plot), run_time=0.8)

    # --- one stride -------------------------------------------------------

    def put(self, line, s, w, color):
        """Line `line` takes way w of set s; returns the animations."""
        anims = []
        old = self.labels[s][w]
        if old is not None:
            anims.append(Fade(old, False))
            anims.append(Succession(self.cells[s][w].animate(run_time=0.25).set_fill(RED, 0.8),
                                    self.cells[s][w].animate(run_time=0.25).set_fill(color, 0.35)))
        else:
            anims.append(self.cells[s][w].animate.set_fill(color, 0.35))
        lab = txt(f'L{line}', 13, TEXT).move_to(self.cells[s][w])
        self.labels[s][w] = lab
        anims.append(Fade(lab, True))
        return anims

    def run(self, stride, n, color, notes):
        """Read n lines at this stride twice, LRU in every set."""
        sets = [[] for _ in range(CACHE.sets)]          # (line, way), oldest first
        notes = dict((k, (a, b)) for k, a, b in notes)
        for rnd in range(2):
            for k in range(n):
                s = CACHE.set_of(k * stride)
                hit = any(line == k for line, _ in sets[s])
                if hit:
                    way = next(w for line, w in sets[s] if line == k)
                    sets[s] = [(line, w) for line, w in sets[s] if line != k] + [(k, way)]
                    c = self.cells[s][way]
                    self.play(Succession(c.animate(run_time=0.15).set_fill(GREEN, 0.9),
                                         c.animate(run_time=0.15).set_fill(color, 0.35)))
                    continue
                if len(sets[s]) == CACHE.ways:
                    _, way = sets[s].pop(0)
                else:
                    way = len(sets[s])
                sets[s].append((k, way))
                self.play(*self.put(k, s, way, color), run_time=0.45 if rnd == 0 else 0.3)
                if rnd == 0:
                    self.dots.add(self.plot.dot(k + 1, conflict_time(CACHE, stride, k + 1), color))
                    self.play(Create(self.dots[-1]), run_time=0.15)
                if rnd == 0 and k + 1 in notes:
                    self.say(*notes[k + 1])
        self.clear()

    def clear(self):
        anims = []
        for s in range(CACHE.sets):
            for w in range(CACHE.ways):
                anims.append(self.cells[s][w].animate.set_fill(SURF, 1))
                if self.labels[s][w] is not None:
                    anims.append(Fade(self.labels[s][w], False))
                    self.labels[s][w] = None
        self.play(*anims, run_time=0.4)

    def finish(self):
        red = self.plot.curve(NS, self.red, RED)
        green = self.plot.curve(NS, self.green, GREEN)
        legend = self.plot.legend([('шаг = банк', RED), ('шаг = банк + линейка', GREEN)],
                                  x=self.plot.x0 + 2.6, y=self.plot.y0 + self.plot.h * 0.5)
        self.say('Красный график поднимается на n = 5: линеек стало больше, чем путей в наборе.',
                 'Значит, степень ассоциативности равна 5 − 1 = 4.',
                 extra=[Create(red, run_time=2), Create(green, run_time=2), Fade(legend, True),
                        Fade(self.dots, False)], run_time=2)
        self.play(Create(self.plot.mark(5, 'n = 5', HL, 13)), run_time=0.5)
        ratio = self.red[-1] / self.green[-1]
        self.say(f'Опыт делит время с конфликтами на время без них. В модели вышло {ratio:.0f}.',
                 'Вывод: шаг, кратный банку кэша, выгодно сдвинуть на одну линейку.')
