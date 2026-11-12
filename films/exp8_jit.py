"""Film 8: DOOM column drawing in two modes. Branch mode reloads the colour map pointer and
checks the loop on every pixel; JIT mode keeps the pointer in a register and draws 8 pixels
per check. The modes switch every second, the server reads the frame times."""
import sys
from pathlib import Path

from manim import LEFT, Arrow, Create, VGroup

sys.path.insert(0, str(Path(__file__).parent))
from common import DIM, GREEN, RED, SUB, SURF, TEXT, Fade, Film, Plot, cell, code, panel, txt  # noqa: E402
from model import jit_frames  # noqa: E402

SX0, SY0, SC = -6.5, 2.6, 0.27      # screen: 12 x 8 pixels from the top-left corner
COLS, ROWS, COL = 12, 8, 5
WALL = ['#fab387', '#f9e2af', '#fab387', '#eba0ac', '#fab387', '#f9e2af', '#eba0ac', '#fab387']
MEM_X, MEM_Y, REG_Y = -1.85, 2.2, 1.0


class Exp8Jit(Film):
    TITLE = 'Опыт 8. Самомодифицирующийся код'

    def story(self):
        self.say('Опыт меряет время кадра игры DOOM в двух режимах рисования.',
                 'Игра пишет время каждого кадра в файл jit_benchmark.csv.')
        self.draw_screen()
        self.branch_mode()
        self.jit_mode()
        self.frames()

    def pix(self, c, r):
        return SX0 + (c + 0.5) * SC, SY0 - (r + 0.5) * SC

    def draw_screen(self):
        self.pixels = [[cell(*self.pix(c, r), SC - 0.02, SC - 0.02) for c in range(COLS)] for r in range(ROWS)]
        screen = VGroup(*[p for row in self.pixels for p in row])
        self.mem = panel(MEM_X, MEM_Y, 2.2, 0.55, 'память')
        self.mem.add(txt('dc_colormap', 14, TEXT).move_to((MEM_X, MEM_Y, 0)))
        self.reg = panel(MEM_X, REG_Y, 2.2, 0.55, 'регистр')
        self.reg_text = txt('пусто', 14, DIM).move_to((MEM_X, REG_Y, 0))
        self.loads, self.checks = 0, 0
        self.counter = self.counters()
        self.play(Create(screen), Create(self.mem), Create(self.reg), Fade(self.reg_text, True),
                  Fade(self.counter, True), run_time=1.2)
        self.say('Стены DOOM рисует вертикальными столбцами. Функция R_DrawColumn рисует один столбец.',
                 'Цвет пикселя берётся из таблицы освещения dc_colormap.')

    def counters(self):
        return VGroup(txt(f'чтений указателя: {self.loads}', 15, SUB),
                      txt(f'проверок условия: {self.checks}', 15, SUB)).arrange(
            (0, -1, 0), aligned_edge=LEFT, buff=0.12).move_to((SX0, 0.05, 0), aligned_edge=LEFT)

    def bump(self, loads, checks):
        self.loads += loads
        self.checks += checks
        new = self.counters()
        anims = [Fade(self.counter, False), Fade(new, True)]
        self.counter = new
        return anims

    def load(self, rt):
        arrow = Arrow(self.mem[0].get_bottom(), self.reg[1].get_top(), color=RED, stroke_width=3, buff=0.04)
        new = txt('dc_colormap', 14, TEXT).move_to((MEM_X, REG_Y, 0))
        self.play(Create(arrow), Fade(self.reg_text, False), Fade(new, True), run_time=rt)
        self.reg_text = new
        self.play(Fade(arrow, False), run_time=rt / 2)

    def clear_column(self, col):
        self.play(*[self.pixels[r][col].animate.set_fill(SURF, 1) for r in range(ROWS)], run_time=0.3)

    def branch_mode(self):
        self.prog = code(['do {', '    colormap = dc_colormap;      // volatile',
                          '    *dest = colormap[source[frac >> 16]];', '    dest += SCREENWIDTH;',
                          '} while (count--);'], -6.5, -0.85, size=14, dy=0.28)
        self.play(Fade(self.prog, True), run_time=0.4)
        self.say('Режим Branch: указатель volatile, процессор читает его из памяти на каждый пиксель.',
                 'После каждого пикселя цикл проверяет условие.')
        for r in range(ROWS):
            rt = 0.3 if r < 2 else 0.15
            self.load(rt)
            self.play(self.pixels[r][COL].animate.set_fill(WALL[r], 0.9), self.prog[4].animate.set_color(RED),
                      *self.bump(1, 1), run_time=rt)
            self.play(self.prog[4].animate.set_color(TEXT), run_time=0.05)
        self.say('Столбец из 8 пикселей: 8 чтений указателя и 8 проверок условия.')
        self.play(Fade(self.prog, False), run_time=0.3)
        self.clear_column(COL)

    def jit_mode(self):
        self.prog = code(['colormap = dc_colormap;          // в регистре', 'while (count >= 7) {',
                          '    *dest = colormap[...]; dest += SCREENWIDTH;', '    ... ещё 7 пикселей ...',
                          '    count -= 8;', '}'], -6.5, -0.85, size=14, dy=0.28)
        self.loads, self.checks = 0, 0
        self.play(Fade(self.prog, True), *self.bump(0, 0), run_time=0.4)
        self.say('Режим JIT: указатель читается один раз и лежит в регистре.',
                 'Цикл развёрнут: один проход рисует 8 пикселей.')
        self.load(0.4)
        self.play(*[self.pixels[r][COL].animate.set_fill(WALL[r], 0.9) for r in range(ROWS)],
                  self.prog[1].animate.set_color(GREEN), *self.bump(1, 1), run_time=0.6)
        self.play(self.prog[1].animate.set_color(TEXT), run_time=0.1)
        self.say('Тот же столбец: одно чтение указателя и одна проверка условия.',
                 'Процессору реже приходится угадывать переход и ждать память.')
        self.play(Fade(self.prog, False), run_time=0.3)

    def frames(self):
        data = jit_frames()
        self.play(Fade(VGroup(*[p for row in self.pixels for p in row], self.mem, self.reg, self.reg_text,
                              self.counter), False), run_time=0.5)
        plot = Plot(-6.4, -2.45, 13.0, 5.2, (0, len(data)), (0.8, 1.6), 'номер кадра', 'время кадра, мс',
                    xticks=[(f, str(f)) for f in range(0, len(data) + 1, 35)],
                    yticks=[(v, f'{v:.1f}') for v in (1.0, 1.2, 1.4)])
        self.play(Create(plot), run_time=0.8)
        segs, start = VGroup(), 0
        for f in range(1, len(data) + 1):
            if f == len(data) or data[f][0] != data[start][0]:
                xs = list(range(start, f))
                color = GREEN if data[start][0] else RED
                segs.add(plot.curve(xs, [data[i][1] for i in xs], color, width=2))
                start = f
        legend = plot.legend([('JIT', GREEN), ('Branch', RED)], x=plot.x0 + 10.5)
        self.say('Режимы меняются каждую секунду, игра идёт как обычно.',
                 'Зелёные участки: JIT, красные: Branch.',
                 extra=[Create(segs, run_time=4), Fade(legend, True)], run_time=4)
        jit = [t for j, t in data if j]
        br = [t for j, t in data if not j]
        speedup = (sum(br) / len(br)) / (sum(jit) / len(jit))
        self.say(f'Ускорение равно среднему времени кадра Branch, делённому на JIT. В модели {speedup:.2f}.',
                 'Вывод: код без лишних чтений памяти и переходов рисует кадр быстрее.')
