"""Film 1: interleaving of DRAM. How the address splits into bank, row and column,
why stride T1 sends every read to one bank and stride T2 reopens a row on every read."""
import sys
from pathlib import Path

from manim import LEFT, RIGHT, AnimationGroup, Create, FadeOut, LaggedStart, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (DIM, GREEN, HL, LINE, PEACH, RED, SUB, SURF, TEXT, YELLOW, Film, Plot, blink, cell, code,  # noqa: E402
                    draw, hide, panel, pop, roll, show, txt)
from model import Dram, stratification_curve, stratification_reads  # noqa: E402

DRAM = Dram(rows=64)          # the curve needs an array much longer than the largest stride
SHOWN_ROWS = 4                # rows drawn in every bank
BANK_X = [-5.45, -3.8, -2.15, -0.5]
BANK_W, CELL_H = 1.5, 0.34
ROW_Y = [1.9, 1.48, 1.06, 0.64]
BUF_Y = 2.47
STEPS = list(range(64, 4097, 64))
T1, T2 = DRAM.banks * DRAM.line, DRAM.banks * DRAM.cols * DRAM.line
CODE_AT = (-6.3, -1.1)


class Exp1Dram(Film):
    TITLE = 'Опыт 1. Расслоение динамической памяти'

    def story(self):
        self.draw_banks()
        self.draw_address()
        self.draw_code_and_plot()
        self.curve = dict(zip(STEPS, stratification_curve(DRAM, STEPS)))
        self.demo(64, 'Шаг 64 байта: чтения идут во все четыре банка сразу.',
                  'Банки работают одновременно, время малое.')
        self.demo(128, 'Шаг 128 байт: чтения попадают только в банки 0 и 2.',
                  'Работают два банка из четырёх, время растёт вдвое.')
        self.demo(T1, 'Шаг 256 байт: все чтения попадают в банк 0.',
                  'Чтения идут по одному, остальные банки стоят. Это точка T1.')
        self.plot_mark(T1, 'T1')
        self.demo(T2, 'Шаг 2048 байт: каждое чтение открывает в банке 0 новую строку.',
                  'Строку закрывают и открывают заново. Это самый долгий шаг, точка T2.', reads=4)
        self.plot_mark(T2, 'T2')
        self.full_curve()
        self.formulas()
        self.say('На реальной ЭВМ банков и строк больше, а массив длиннее.',
                 'Пики стоят на других шагах, но график читается так же.')

    # --- memory ---------------------------------------------------------

    def draw_banks(self):
        self.banks, self.cells, self.bufs, self.buf_txt = VGroup(), [], [], []
        for b, x in enumerate(BANK_X):
            g = VGroup(txt(f'банк {b}', 17, SUB).move_to((x, 2.95, 0)))
            buf = cell(x, BUF_Y, BANK_W, 0.32, SURF, stroke=DIM)
            label = txt('строка закрыта', 12, DIM).move_to(buf)
            rows = []
            for r, y in enumerate(ROW_Y):
                row = VGroup(*[cell(x - BANK_W / 2 + (c + 0.5) * BANK_W / DRAM.cols, y, BANK_W / DRAM.cols - 0.02,
                                    CELL_H) for c in range(DRAM.cols)])
                rows.append(row)
                g.add(row)
            g.add(buf, label)
            self.cells.append(rows)
            self.bufs.append(buf)
            self.buf_txt.append(label)
            self.banks.add(g)
        self.row_names = VGroup(*[txt(f'строка {r}', 12, DIM).move_to((BANK_X[0] - BANK_W / 2 - 0.08, y, 0),
                                                                    aligned_edge=RIGHT)
                                  for r, y in enumerate(ROW_Y)])
        appear = LaggedStart(*[show(g, lag=0.12) for g in self.banks], show(self.row_names), lag_ratio=0.15,
                             run_time=2.2)
        self.say('Опыт находит, как адрес делится на банк, строку и столбец памяти.',
                 'По этим долям видно, как устроена оперативная память.', extra=[appear], run_time=2.2)
        self.say('Память состоит из банков. Банки работают одновременно.',
                 'В каждом банке строки, в строке столбцы. Столбец хранит одну линейку кэша, 64 байта.')
        self.spot([self.bufs[0]], 'Банк читает строку целиком в строковый буфер.',
                  'Чтение из открытой строки быстрое. Смена строки медленная.')

    def draw_address(self):
        fields = [('строка', 2.4, 'старшие'), ('столбец', 1.4, '10…8'), ('банк', 1.0, '7…6'),
                  ('смещение', 1.6, '5…0')]
        x, y = BANK_X[0] - BANK_W / 2, -0.15
        self.addr = VGroup()
        colors = [LINE, LINE, PEACH, LINE]
        for (name, w, bits), color in zip(fields, colors):
            box = cell(x + w / 2, y, w, 0.36, SURF, stroke=color if color != LINE else SUB)
            self.addr.add(VGroup(box, txt(name, 14, TEXT).move_to(box), txt(bits, 12, DIM).move_to((x + w / 2, y - 0.33, 0))))
            x += w
        self.addr.add(txt('адрес', 14, SUB).next_to(self.addr[0], LEFT, buff=0.12))
        self.play(show(self.addr, lag=0.15, shift=(0, 0.2, 0)), run_time=1.2)
        self.spot([self.addr[2]], 'Младшие 6 бит адреса выбирают байт в линейке, следующие 2 бита выбирают банк.',
                  'Поэтому соседние линейки лежат в соседних банках.')
        # Lines 0..7 queue up in address order, then fly into banks 0, 1, 2, 3, 0, 1, 2, 3
        queue = VGroup(*[VGroup(cell(-5.9 + n * 0.42, -0.95, 0.36, 0.32, YELLOW, opacity=0.35, stroke=YELLOW),
                                txt(str(n), 13, TEXT).move_to((-5.9 + n * 0.42, -0.95, 0)))
                         for n in range(8)])
        label = txt('линейки по порядку адресов', 13, SUB).next_to(queue, (1, 0, 0), buff=0.2)
        self.play(show(queue, lag=0.08), show(label), run_time=1)
        flights = []
        for n, item in enumerate(queue):
            bank, row, col = DRAM.place(n * DRAM.line)
            target = self.cells[bank][row][col]
            box, num = item
            flights.append(AnimationGroup(
                box.animate(path_arc=-0.45).move_to(target).stretch_to_fit_width(target.width)
                .stretch_to_fit_height(target.height),
                num.animate(path_arc=-0.45).move_to(target).scale(0.9)))
        self.say('Линейки 0, 1, 2, 3 легли в банки 0, 1, 2, 3, линейка 4 снова в банк 0.',
                 extra=[LaggedStart(*flights, lag_ratio=0.18, run_time=2.4), FadeOut(label)], run_time=2.4)
        self.play(hide(queue), run_time=0.5)

    def draw_code_and_plot(self):
        self.code = code(self.listing('step'), *CODE_AT, size=16)
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (0, 4096), (0, 6500), 'шаг, байт', 'время чтения массива',
                         xticks=[(256, '256'), (1024, '1024'), (2048, '2048'), (3072, '3072'), (4096, '4096')])
        self.play(show(self.code, lag=0.15), self.plot.appear(), run_time=1.4)
        self.say('Программа читает массив с шагом step. При любом шаге число чтений одинаково.',
                 'График показывает время чтения всего массива для каждого шага.')

    # --- one stride -------------------------------------------------------

    @staticmethod
    def listing(step):
        """The experiment loop; a number in place of step is lit in the colour of the reads."""
        lit = PEACH if isinstance(step, int) else TEXT
        return [(f'for (b = 0; b < {step}; b += 64)', lit), (f'    for (a = b; a < size; a += {step})', lit),
                '        x += p[a];']

    def demo(self, step, line, second, reads=8):
        """The first reads of the loop at this stride: reads that run at once light up together."""
        addrs = stratification_reads(DRAM, step, inner=reads)
        groups = self.waves(addrs)
        self.code, recode = self.recode(self.code, self.listing(step), *CODE_AT, size=16)
        self.say(line, second, hold=0, extra=recode)
        lit = []
        for group in groups:
            anims = []
            for addr in group:
                bank, row, col = DRAM.place(addr)
                if row >= SHOWN_ROWS:
                    continue
                c = self.cells[bank][row][col]
                lit.append(c)
                anims.append(c.animate.set_fill(PEACH, 0.9).scale(1.08))
                anims += self.open_row(bank, row)
            if anims:
                self.play(*anims, run_time=0.45 if len(group) > 1 else 0.3)
        dot = self.plot.dot(step, self.curve[step], PEACH)
        self.play(pop(dot), run_time=0.5)
        self.wait(2.5)
        self.dots = getattr(self, 'dots', VGroup())
        self.dots.add(dot)
        self.play(*[c.animate.set_fill(SURF, 1).scale(1 / 1.08) for c in lit], *self.close_rows(), run_time=0.5)

    def waves(self, addrs):
        """Split reads into steps of the film: reads to different banks run together,
        a read to a busy bank starts a new step."""
        groups, busy = [], set()
        for a in addrs:
            bank = DRAM.place(a)[0]
            if not groups or bank in busy or len(groups[-1]) == DRAM.window:
                groups.append([])
                busy = set()
            groups[-1].append(a)
            busy.add(bank)
        return groups

    def open_row(self, bank, row):
        old = self.buf_txt[bank]
        if getattr(old, 'row', None) == row:
            return []
        new = txt(f'строка {row}', 12, TEXT).move_to(self.bufs[bank])
        new.row = row
        self.buf_txt[bank] = new
        flash = RED if getattr(old, 'row', None) is not None else GREEN
        return [*roll(old, new), blink(self.bufs[bank], flash, 0.6)]

    def close_rows(self):
        anims = []
        for b in range(DRAM.banks):
            old = self.buf_txt[b]
            if getattr(old, 'row', None) is None:
                continue
            new = txt('строка закрыта', 12, DIM).move_to(self.bufs[b])
            self.buf_txt[b] = new
            anims += roll(old, new)
        return anims

    # --- graph ----------------------------------------------------------

    def plot_mark(self, x, name):
        m = self.plot.mark(x, name, HL)
        self.play(draw(m), run_time=0.6)

    def full_curve(self):
        line = self.plot.curve(STEPS, [self.curve[s] for s in STEPS], PEACH, width=2.5)
        self.code, recode = self.recode(self.code, self.listing('step'), *CODE_AT, size=16)
        self.say('Так программа проходит все шаги от 64 байт до 4 КБ.',
                 'Пики стоят на шагах, кратных T1, и растут до T2. После T2 пики не растут.',
                 extra=[Create(line, run_time=3), *recode], run_time=3)
        self.play(FadeOut(self.dots, scale=0.3), run_time=0.4)

    def formulas(self):
        box = panel(-3.6, -2.25, 5.6, 1.05)
        lines = VGroup(txt('Б = T1 / П = 256 / 64 = 4 банка', 16, TEXT),
                       txt('РС = T2 / Б = 2048 / 4 = 512 байт', 16, TEXT),
                       txt('С = О / РС', 16, TEXT)).arrange((0, -1, 0), aligned_edge=LEFT, buff=0.1)
        lines.move_to(box)
        self.play(hide(self.code), draw(box), show(lines, lag=0.2), run_time=1.0)
        self.spot([lines[0]], 'Число банков Б: шаг T1 делим на размер линейки П.',
                  'При шаге T1 чтения впервые все попадают в один банк.')
        self.spot([lines[1]], 'Размер страницы РС: шаг T2 делим на число банков.',
                  'Шаг T2 равен расстоянию между соседними строками одного банка.')
        self.spot([lines[2]], 'Число страниц С: объём физической памяти О делим на размер страницы.',
                  'Объём памяти выводит ячейка «Идентификация процессора».')
