"""Film 3: software prefetch. The first read of every page waits for the page table walk;
a loop that touches one word per page fills the TLB before the measured loop."""
import sys
from pathlib import Path

from manim import Arrow, Create, FadeOut, LaggedStart, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import (BLUE, DIM, GREEN, PEACH, RED, SUB, SURF, TEXT, YELLOW, Film, Plot, blink, cell, code,  # noqa: E402
                    hide, pop, roll, show, txt)
from model import PAGE, tlb_reads, tlb_times  # noqa: E402

PAGES, PER_PAGE = 4, 16                       # 4 pages of 4 KB, one read per 256 bytes
PX0, PW, PY = -6.0, 1.45, 1.55                # page boxes
TLB_X, TLB_Y = -5.6, -0.55
TABLE_X = -2.4


class Exp3Tlb(Film):
    TITLE = 'Опыт 3. Программная предвыборка'

    def story(self):
        self.offs = tlb_reads()
        self.cold, self.warm = tlb_times(self.offs, False), tlb_times(self.offs, True)
        self.draw_pages()
        self.draw_tlb()
        self.draw_plot()
        self.cold_pass()
        self.prefetch_pass()
        self.warm_pass()
        self.finish()

    # --- scene ----------------------------------------------------------

    def draw_pages(self):
        self.pages, self.cells = VGroup(), []
        for p in range(PAGES):
            x = PX0 + p * (PW + 0.15)
            box = cell(x, PY, PW, 1.3, SURF, opacity=0, stroke=SUB)
            cs = VGroup(*[cell(x - PW / 2 + 0.11 + (k % 4) * 0.41, PY + 0.45 - (k // 4) * 0.3, 0.36, 0.24)
                          for k in range(PER_PAGE)])
            self.cells.append(cs)
            self.pages.add(VGroup(box, cs, txt(f'страница {p}', 14, SUB).move_to((x, PY + 0.85, 0))))
        appear = LaggedStart(*[show(pg, lag=0.15) for pg in self.pages], lag_ratio=0.2, run_time=2)
        self.say('Опыт показывает, сколько стоит первое обращение к странице памяти.',
                 'И как убрать эту задержку из основного цикла.', extra=[appear], run_time=2)
        self.say('Блок памяти занимает 4 страницы по 4 КБ. Программа читает слово каждые 256 байт.',
                 'В каждой странице 16 чтений.')

    def draw_tlb(self):
        rows = VGroup()
        self.tlb_rows = []
        for r in range(PAGES):
            c = cell(TLB_X, TLB_Y + 0.45 - r * 0.32, 1.6, 0.3, SURF)
            t = txt('пусто', 12, DIM).move_to(c)
            rows.add(c, t)
            self.tlb_rows.append([c, t])
        self.tlb = VGroup(rows, txt('TLB', 16, SUB).move_to((TLB_X, TLB_Y + 0.85, 0)))
        table = cell(TABLE_X, TLB_Y, 1.9, 1.15, SURF, stroke=SUB)
        self.table = VGroup(table, txt('таблица страниц', 14, SUB).move_to((TABLE_X, TLB_Y + 0.3, 0)),
                            txt('в памяти', 14, SUB).move_to((TABLE_X, TLB_Y, 0)),
                            txt('2–4 чтения', 12, DIM).move_to((TABLE_X, TLB_Y - 0.33, 0)))
        self.play(LaggedStart(show(self.tlb[1], self.tlb[0], lag=0.1), show(self.table, lag=0.15), lag_ratio=0.4),
                  run_time=1.4)
        self.say('Процессор переводит адрес программы в физический по таблице страниц.',
                 'Последние переводы хранит маленький буфер TLB. Чтение таблицы страниц долгое.')

    def draw_plot(self):
        self.plot = Plot(1.0, -2.45, 5.85, 4.7, (0, PAGES * PAGE), (0, 10), 'смещение, байт', 'время чтения',
                         xticks=[(p * PAGE, f'{p * 4} К') for p in range(PAGES + 1)])
        self.play(self.plot.appear(), run_time=1.2)

    # --- passes ---------------------------------------------------------

    def walk(self, p, fast=False):
        """TLB miss on page p: read the page table, write the translation into the TLB."""
        c, t = self.tlb_rows[p]
        arrow = Arrow(c.get_right(), self.table[0].get_left(), color=RED, stroke_width=3, buff=0.05,
                      max_tip_length_to_length_ratio=0.15)
        new = txt(f'страница {p}', 12, TEXT).move_to(c)
        self.tlb_rows[p][1] = new
        rt = 0.25 if fast else 0.6
        self.play(Create(arrow), blink(self.table[0], RED, 0.35), run_time=2 * rt)
        self.play(*roll(t, new), c.animate.set_fill(BLUE, 0.35), FadeOut(arrow), run_time=rt)

    def read_page(self, p, times, color, first_red, fast):
        """The 16 reads of page p and their points on the graph."""
        dots = VGroup()
        for k in range(PER_PAGE):
            i = p * PER_PAGE + k
            dots.add(self.plot.dot(self.offs[i], times[i], color if not (k == 0 and first_red) else RED, r=0.045))
        self.play(self.cells[p][0].animate.set_fill(RED if first_red else color, 0.85), pop(dots[0]),
                  run_time=0.4)
        rest = LaggedStart(*[c.animate.set_fill(color, 0.85) for c in self.cells[p][1:]], lag_ratio=0.15)
        self.play(rest, LaggedStart(*[pop(d) for d in dots[1:]], lag_ratio=0.15), run_time=0.6 if fast else 1.4)
        return dots

    def clear(self, extra=()):
        anims = [c.animate.set_fill(SURF, 1) for cs in self.cells for c in cs]
        for row in self.tlb_rows:
            new = txt('пусто', 12, DIM).move_to(row[0])
            anims += [*roll(row[1], new), row[0].animate.set_fill(SURF, 1)]
            row[1] = new
        self.play(*anims, *extra, run_time=0.5)

    def cold_pass(self):
        prog = code(['for (a = 0; a < size; a += step)', '    x += p[a];'], -6.55, -1.85)
        self.play(show(prog, lag=0.2), run_time=0.6)
        self.say('Первое чтение страницы не находит её в TLB и ждёт таблицу страниц.',
                 'Остальные 15 чтений быстрые: перевод уже лежит в TLB.', hold=0)
        self.cold_dots = VGroup()
        for p in range(PAGES):
            self.walk(p, fast=p > 0)
            self.cold_dots.add(self.read_page(p, self.cold, PEACH, True, fast=p > 0))
            if p == 0:
                self.wait(2.5)
        self.say('Без предвыборки пики стоят через 4 КБ: на первом чтении каждой страницы.',
                 'Высота пика равна времени первого обращения к странице.')
        self.clear([hide(prog)])
        self.prog = prog

    def prefetch_pass(self):
        prog = code([('for (i = 0; i < size; i += 4096)', YELLOW), ('    x += p[i];', YELLOW),
                     'for (a = 0; a < size; a += step)', '    x += p[a];'], -6.55, -1.85)
        self.play(show(prog, lag=0.15), run_time=0.7)
        self.say('Цикл предвыборки читает одно слово из каждой страницы.',
                 'Переводы всех страниц попадают в TLB до основного цикла.', hold=0)
        for p in range(PAGES):
            self.play(self.cells[p][0].animate.set_fill(YELLOW, 0.85), run_time=0.3)
            self.walk(p, fast=True)
        self.wait(1.5)
        self.play(*[cs[0].animate.set_fill(SURF, 1) for cs in self.cells], run_time=0.3)
        self.prog = prog

    def warm_pass(self):
        self.say('Теперь основной цикл не ждёт таблицу страниц. Зелёные точки лежат ровно.',
                 'Время цикла предвыборки опыт тоже выводит: оно входит в цену способа.', hold=0)
        self.warm_dots = VGroup()
        for p in range(PAGES):
            self.warm_dots.add(self.read_page(p, self.warm, GREEN, False, fast=True))
        self.wait(2)

    def finish(self):
        ratio = sum(self.cold) / sum(self.warm)
        self.say(f'Опыт делит время без предвыборки на время с ней. В модели вышло {ratio:.2f}.',
                 'Предвыборка выгодна, когда страниц много, а чтений в каждой мало.')
        self.say('Если страниц больше, чем записей в TLB, ранние переводы вытесняются.',
                 'Поэтому размер TLB данных входит в исходные данные опыта.')
