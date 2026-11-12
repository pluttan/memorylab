"""Film 6: cache performance. A block grows; while it fits a cache level the time per KB
stays flat, past the level it steps up. Four modes: read, write, read and write, write then read."""
import sys
from pathlib import Path

import numpy as np
from manim import (RIGHT, UP, GrowFromPoint, smooth, AnimationGroup, Create, FadeIn, LaggedStart, Rectangle,
                   ValueTracker, VGroup, always_redraw)

sys.path.insert(0, str(Path(__file__).parent))
from common import (BLUE, GREEN, HL, MAUVE, RED, SUB, SURF, YELLOW, Film, Plot, code, draw, hide, polyline,  # noqa: E402
                    show, txt)
from model import LEVELS, cache_curve  # noqa: E402

LO, HI = 1 << 10, 128 << 20                       # block sizes on the graph: 1 KB .. 128 MB
CORNER = np.array((-6.6, 2.85, 0.0))              # the core sits in this corner, levels grow from it
W0, H0, W1, H1 = 0.4, 0.25, 6.0, 3.3
NAMES = ['L1', 'L2', 'L3', 'ОЗУ']
SIZES = [LEVELS[0], LEVELS[1], LEVELS[2], HI]
SIZE_TEXT = ['32 КБ', '256 КБ', '9 МБ', '']
MODE_COLORS = [('read', 'чтение', RED), ('write', 'запись', GREEN), ('read_write', 'чтение и запись', MAUVE),
               ('write_read', 'запись, затем чтение', BLUE)]


def frac(size):
    return (np.log2(size) - np.log2(LO)) / (np.log2(HI) - np.log2(LO))


def extent(size):
    f = frac(size)
    return W0 + f * (W1 - W0), H0 + f * (H1 - H0)


def size_text(s):
    if s >= 1 << 20:
        return f'{s / (1 << 20):.0f} МБ' if s >= 10 << 20 else f'{s / (1 << 20):.1f} МБ'
    return f'{s / 1024:.0f} КБ'


class Exp6Cache(Film):
    TITLE = 'Опыт 6. Производительность кэш-памяти'

    def story(self):
        self.draw_levels()
        self.draw_plot()
        self.grow()
        self.modes()
        self.say('Ступени графика дают объёмы L1, L2 и L3.',
                 'Сравните их с ячейкой «Идентификация процессора».')

    def draw_levels(self):
        self.levels = VGroup()
        for name, size, label in reversed(list(zip(NAMES, SIZES, SIZE_TEXT))):
            w, h = extent(size)
            box = Rectangle(width=w, height=h, stroke_color=SUB, stroke_width=2, fill_color=SURF, fill_opacity=0.35)
            box.move_to(CORNER + (w / 2, -h / 2, 0))
            t = txt(f'{name} {label}'.strip(), 15, SUB).move_to(CORNER + (w - 0.1, -h + 0.18, 0),
                                                                aligned_edge=RIGHT)
            self.levels.add(VGroup(box, t))
        core = txt('ядро', 13, HL).move_to(CORNER + (0.2, -0.12, 0))
        grow = LaggedStart(FadeIn(core, scale=0.5),
                           *[AnimationGroup(GrowFromPoint(lv[0], CORNER), FadeIn(lv[1], shift=UP * 0.1))
                             for lv in reversed(self.levels)], lag_ratio=0.35)
        self.say('Опыт обрабатывает блок памяти всё большего размера.',
                 'Он меряет время обработки одного килобайта блока.', extra=[grow], run_time=2.4)
        self.say('Кэш состоит из уровней. Каждый следующий уровень больше и медленнее.',
                 'Объёмы взяты у Intel Core i5-8400, на нём сняты и времена графика.')
        self.prog = code(['for (pass = 0; pass < passes; pass++)', '    for (a = 0; a < size; a += 64)',
                          '        x += p[a];'], -6.5, -0.95)
        self.play(show(self.prog, lag=0.2), run_time=0.7)
        self.say('Программа читает блок по кругу, одно слово на линейку кэша.',
                 'После первого прохода блок лежит в самом быстром уровне, куда он влез.')

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (LO, HI), (0, 120), 'размер блока', 'время на 1 КБ, нс',
                         xticks=[(LO, '1 К'), (32 << 10, '32 К'), (256 << 10, '256 К'), (9 << 20, '9 М'),
                                 (HI, '128 М')], xlog=True)
        self.play(self.plot.appear(), run_time=1.2)

    def grow(self):
        f = ValueTracker(np.log2(LO))
        alpha = ValueTracker(0.0)               # always_redraw ignores FadeIn, so the block fades by hand
        sizes = np.logspace(np.log2(LO), np.log2(HI), 120, base=2)
        reads = cache_curve(sizes, 'read')

        def block():
            s = 2 ** f.get_value()
            w, h = extent(s)
            a = alpha.get_value()
            r = Rectangle(width=w, height=h, stroke_width=0, fill_color=YELLOW, fill_opacity=0.28 * a)
            label = txt(f'блок {size_text(s)}', 15, YELLOW).move_to(CORNER + (W1, 0.22, 0), aligned_edge=RIGHT)
            return VGroup(r.move_to(CORNER + (w / 2, -h / 2, 0)), label.set_opacity(a))

        def trail():
            s = 2 ** f.get_value()
            k = int(np.searchsorted(sizes, s))
            pts = [self.plot.at(x, y)[:2] for x, y in zip(sizes[:k], reads[:k])]
            pts.append(self.plot.at(s, cache_curve([s], 'read')[0])[:2])
            return polyline(pts if len(pts) > 1 else pts * 2, RED)

        b, t = always_redraw(block), always_redraw(trail)
        self.add(b, t)
        self.play(alpha.animate.set_value(1), run_time=0.6)
        stops = [(16 << 10, 'Блок 16 КБ помещается в L1: время на килобайт малое.', ''),
                 (128 << 10, 'Блок больше 32 КБ: часть линеек вытесняется из L1 и читается из L2.',
                  'Время на килобайт растёт ступенью.'),
                 (4 << 20, 'Блок больше 256 КБ живёт в L3. Ещё одна ступень.', ''),
                 (HI, 'Блок больше 9 МБ читается из оперативной памяти.', 'Это самая высокая ступень.')]
        for size, line, second in stops:
            self.say(line, second, hold=0)
            self.play(f.animate.set_value(np.log2(size)), run_time=3, rate_func=smooth)
            self.wait(1.2)
        self.play(alpha.animate.set_value(0), run_time=0.6)
        b.clear_updaters()
        t.clear_updaters()
        self.remove(b)
        self.trail = t

    def modes(self):
        sizes = np.logspace(np.log2(LO), np.log2(HI), 120, base=2)
        curves = [self.plot.curve(sizes, cache_curve(sizes, m), c, width=2.5) for m, _, c in MODE_COLORS[1:]]
        legend = self.plot.legend([(n, c) for _, n, c in MODE_COLORS], y=self.plot.y0 + self.plot.h * 0.62)
        self.say('Опыт повторяет то же для записи и для смешанных режимов.',
                 'Запись дороже чтения: изменённую линейку потом пишут обратно в следующий уровень.',
                 extra=[*[Create(c, run_time=2) for c in curves], show(legend, lag=0.15)], run_time=2)
        self.say('Режим «запись, затем чтение» проходит блок дважды, поэтому его линия выше всех.',
                 'Ступени у всех четырёх линий стоят на одних и тех же размерах.')
        marks = VGroup(*[self.plot.mark(s, n, HL, 13) for s, n in zip(LEVELS, NAMES)])
        self.play(LaggedStart(*[draw(m) for m in marks], lag_ratio=0.3), hide(self.prog), run_time=1)
